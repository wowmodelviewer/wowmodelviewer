// TerrainMeshTests.cs
//
// Unit tests of the terrain mesh builder (Assets/Scripts/Wow/World/TerrainMesh.cs) and the mesh dump
// writer (TerrainDump.cs) on synthetic roots from AdtSynthetic: no game content, no file IO, so the
// suite also compiles outside the runner. Framework-free like WorldParserTests: RunAll returns the
// failure count. The same builder on real client bytes is TerrainCorpusTests.cs.
//
// What is proven here (stage 03 design, sections 2 and 3): the pinned coordinate expressions and the
// single world -> Unity map; vertex counts; the hole rule bit by bit; the cell structure and winding
// (with checks that hold for heights, not just for the grid); orientation by asymmetric ramps; the
// tile-wide normal rule, re-derived independently; referenced-vertex bounds; the tile-identity
// tolerance and its float32 generator; every rejection path; no mutable static state.

using System;
using System.Collections.Generic;
using System.Reflection;
using System.Threading;
using Wmv.Wow;
using Wmv.Wow.World;
using static Wmv.Wow.World.AdtSynthetic;

namespace Wmv.Wow.Tests
{
    public static class TerrainMeshTests
    {
        const double T = TerrainCoordinates.TileSize;
        const double C = TerrainCoordinates.ChunkSize;
        const double U = TerrainCoordinates.UnitSize;

        static int failures;
        static readonly List<string> log = new List<string>();

        static void Check(bool condition, string name)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name); failures++; }
        }

        static void Throws<TEx>(Action action, string name, string mustContain = null) where TEx : Exception
        {
            try
            {
                action();
                log.Add("  FAIL  " + name + " (expected " + typeof(TEx).Name + ", nothing was thrown)");
                failures++;
            }
            catch (TEx e)
            {
                if (mustContain != null && e.Message.IndexOf(mustContain, StringComparison.Ordinal) < 0)
                {
                    log.Add("  FAIL  " + name + " (message lacks '" + mustContain + "': " + e.Message + ")");
                    failures++;
                }
                else
                    log.Add("  PASS  " + name);
            }
            catch (Exception e)
            {
                log.Add("  FAIL  " + name + " (expected " + typeof(TEx).Name + ", got " + e.GetType().Name + ": " + e.Message + ")");
                failures++;
            }
        }

        static string G(double v) { return v.ToString("R", System.Globalization.CultureInfo.InvariantCulture); }

        public static TerrainTileMesh BuildBytes(byte[] root, int tileX, int tileY)
        {
            return TerrainMeshBuilder.Build(AdtParser.ParseRoot(root, tileX, tileY), tileX, tileY, WorldToUnity.TileOrigin(tileX, tileY));
        }

        // ================================================================================== shared geometry checks

        /// <summary>Structural checks that hold for any heights: every index 0..144; each cell's triangles
        /// start with its inner vertex and use only its 5 vertices; Cross.y within 1e-3 of U^2/2 per
        /// triangle; projected areas of each cell sum to U^2; no duplicate triangle. Returns a problem or null.</summary>
        public static string CellStructureProblem(TerrainTileMesh tile, out int triangles, out double maxCrossYError)
        {
            triangles = 0;
            maxCrossYError = 0;
            double half = U * U / 2;
            var cell = new int[12];
            foreach (TerrainChunkMesh ch in tile.Chunks)
            {
                if (ch.State != TerrainChunkState.Built)
                    continue;
                if (ch.Indices.Length != ch.Triangles * 3 || ch.Triangles != 4 * (64 - ch.HoleCells))
                    return "chunk " + ch.Index + ": " + ch.Indices.Length + " indices for " + ch.Triangles + " triangles, " + ch.HoleCells + " hole cells";
                var seen = new HashSet<long>();
                int t = 0;
                for (int r = 0; r < 8; r++)
                    for (int c = 0; c < 8; c++)
                    {
                        if (((ch.HoleMask >> (r * 8 + c)) & 1UL) != 0)
                            continue;
                        TerrainMeshBuilder.CellIndices(r, c, cell);
                        var allowed = new HashSet<int>(cell);
                        double area = 0;
                        for (int q = 0; q < 4; q++, t++)
                        {
                            int i0 = ch.Indices[3 * t], i1 = ch.Indices[3 * t + 1], i2 = ch.Indices[3 * t + 2];
                            if (i0 > 144 || i1 > 144 || i2 > 144)
                                return "chunk " + ch.Index + ": index outside 0..144";
                            if (i0 != TerrainVertexLayout.Inner(r, c))
                                return string.Format("chunk {0} cell ({1},{2}) triangle {3} starts with {4}, not the inner index {5}", ch.Index, r, c, q, i0, TerrainVertexLayout.Inner(r, c));
                            if (!allowed.Contains(i1) || !allowed.Contains(i2) || i1 == i2 || i1 == i0 || i2 == i0)
                                return string.Format("chunk {0} cell ({1},{2}) triangle {3} uses a vertex outside the cell", ch.Index, r, c, q);
                            int a = Math.Min(i0, Math.Min(i1, i2)), b = Math.Max(i0, Math.Max(i1, i2)), m = i0 + i1 + i2 - a - b;
                            if (!seen.Add(((long)a << 20) | ((long)m << 10) | (long)b))
                                return "chunk " + ch.Index + ": duplicate triangle";
                            double cy = CrossY(ch.Positions, i0, i1, i2);
                            maxCrossYError = Math.Max(maxCrossYError, Math.Abs(cy - half));
                            if (Math.Abs(cy - half) > 1e-3)
                                return string.Format("chunk {0} cell ({1},{2}) triangle {3}: Cross.y = {4}, expected {5}", ch.Index, r, c, q, G(cy), G(half));
                            area += Math.Abs(cy) / 2;
                        }
                        if (Math.Abs(area - U * U) > 1e-3)
                            return string.Format("chunk {0} cell ({1},{2}): projected area {3}, expected U^2 = {4}", ch.Index, r, c, G(area), G(U * U));
                    }
                triangles += t;
            }
            return null;
        }

        static double CrossY(float[] p, int i0, int i1, int i2)
        {
            double e1x = (double)p[3 * i1] - p[3 * i0], e1z = (double)p[3 * i1 + 2] - p[3 * i0 + 2];
            double e2x = (double)p[3 * i2] - p[3 * i0], e2z = (double)p[3 * i2 + 2] - p[3 * i0 + 2];
            return e1z * e2x - e1x * e2z;
        }

        /// <summary>Chunk-border copies: positions x/z and normals bit-equal at every outer vertex two
        /// Built chunks share. Returns a problem or null; `points` counts the compared pairs.</summary>
        public static string BorderProblem(TerrainTileMesh tile, out int points)
        {
            points = 0;
            for (int iy = 0; iy < 16; iy++)
                for (int ix = 0; ix < 16; ix++)
                {
                    TerrainChunkMesh a = tile.Chunks[iy * 16 + ix];
                    if (a.State != TerrainChunkState.Built)
                        continue;
                    if (ix < 15)
                    {
                        TerrainChunkMesh b = tile.Chunks[iy * 16 + ix + 1];
                        if (b.State == TerrainChunkState.Built)
                            for (int r = 0; r <= 8; r++, points++)
                            {
                                string p = SamePoint(a, TerrainVertexLayout.Outer(r, 8), b, TerrainVertexLayout.Outer(r, 0));
                                if (p != null) return p;
                            }
                    }
                    if (iy < 15)
                    {
                        TerrainChunkMesh b = tile.Chunks[(iy + 1) * 16 + ix];
                        if (b.State == TerrainChunkState.Built)
                            for (int c = 0; c <= 8; c++, points++)
                            {
                                string p = SamePoint(a, TerrainVertexLayout.Outer(8, c), b, TerrainVertexLayout.Outer(0, c));
                                if (p != null) return p;
                            }
                    }
                }
            return null;
        }

        static string SamePoint(TerrainChunkMesh a, int ka, TerrainChunkMesh b, int kb)
        {
            if (Bits(a.Positions[3 * ka]) != Bits(b.Positions[3 * kb]) || Bits(a.Positions[3 * ka + 2]) != Bits(b.Positions[3 * kb + 2]))
                return string.Format("chunks {0}/{1}: shared vertex x/z differ", a.Index, b.Index);
            for (int i = 0; i < 3; i++)
                if (Bits(a.Normals[3 * ka + i]) != Bits(b.Normals[3 * kb + i]))
                    return string.Format("chunks {0}/{1}: normals of a shared vertex differ ({2} vs {3})", a.Index, b.Index, a.Normals[3 * ka + i], b.Normals[3 * kb + i]);
            return null;
        }

        static int Bits(float f) { return BitConverter.SingleToInt32Bits(f); }

        /// <summary>The builder's normals against an independent derivation: for every Built chunk and
        /// MCVT entry, the normalised sum of the Unity-space cross products (double, mesh order) of the
        /// kept triangles that touch that grid point, found by walking every chunk's cells again with
        /// positions recomputed from VertexWorldXY and the root's heights. Max component error.</summary>
        public static double NormalError(TerrainTileMesh tile, AdtRoot root, out int compared, out int defaults)
        {
            compared = 0;
            defaults = 0;
            WorldOrigin o = tile.Origin;
            // Grid point -> accumulated normal, recomputed: outer points by tile grid, inner by chunk/cell.
            var outer = new Dictionary<int, double[]>();
            var inner = new Dictionary<int, double[]>();
            var cell = new int[12];
            foreach (TerrainChunkMesh ch in tile.Chunks)
            {
                if (ch.State != TerrainChunkState.Built)
                    continue;
                AdtRootChunk mc = root.Mcnk[ch.Index];
                for (int r = 0; r < 8; r++)
                    for (int c = 0; c < 8; c++)
                    {
                        if (mc.Holes.IsHole(r, c) == true)
                            continue;
                        TerrainMeshBuilder.CellIndices(r, c, cell);
                        for (int q = 0; q < 4; q++)
                        {
                            var p = new double[3][];
                            for (int v = 0; v < 3; v++)
                            {
                                int k = cell[3 * q + v];
                                double x, y, ux, uy, uz;
                                TerrainMeshBuilder.VertexWorldXY(tile.TileX, tile.TileY, ch.IndexX, ch.IndexY, k, out x, out y);
                                WorldToUnity.PositionD(x, y, (double)mc.StoredPosition.C + (double)mc.Heights[k], o, out ux, out uy, out uz);
                                p[v] = new[] { ux, uy, uz };
                            }
                            double e1x = p[1][0] - p[0][0], e1y = p[1][1] - p[0][1], e1z = p[1][2] - p[0][2];
                            double e2x = p[2][0] - p[0][0], e2y = p[2][1] - p[0][1], e2z = p[2][2] - p[0][2];
                            double nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
                            for (int v = 0; v < 3; v++)
                            {
                                double[] acc = Acc(outer, inner, ch, cell[3 * q + v]);
                                acc[0] += nx; acc[1] += ny; acc[2] += nz;
                            }
                        }
                    }
            }
            double worst = 0;
            foreach (TerrainChunkMesh ch in tile.Chunks)
            {
                if (ch.State != TerrainChunkState.Built)
                    continue;
                for (int k = 0; k < 145; k++)
                {
                    double[] acc = Acc(outer, inner, ch, k);
                    double l = Math.Sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
                    double[] want = l > 0 ? new[] { acc[0] / l, acc[1] / l, acc[2] / l } : new[] { 0.0, 1.0, 0.0 };
                    if (l == 0) defaults++;
                    for (int i = 0; i < 3; i++)
                        worst = Math.Max(worst, Math.Abs(ch.Normals[3 * k + i] - want[i]));
                    compared++;
                }
            }
            return worst;
        }

        static double[] Acc(Dictionary<int, double[]> outer, Dictionary<int, double[]> inner, TerrainChunkMesh ch, int k)
        {
            int r = k / 17, j = k % 17;
            Dictionary<int, double[]> d;
            int key;
            if (j < 9) { d = outer; key = (8 * ch.IndexY + r) * 1000 + 8 * ch.IndexX + j; }
            else { d = inner; key = ch.Index * 100 + r * 8 + (j - 9); }
            double[] a;
            if (!d.TryGetValue(key, out a)) d[key] = a = new double[3];
            return a;
        }

        /// <summary>The builder's STORED vertices against the pinned double expression: max |stored - local| over
        /// x and z of every vertex of a Built chunk (the error design 2.4 bounds by 2.1e-5 yd), with local the
        /// double (VertexWorldXY + PositionD relative to tile.Origin). `bitExact` is set when every stored x, y
        /// and z equals the float cast of that double bit for bit (y = (float)((double)C + h - Z0)); a NaN or
        /// a wrong-length array makes the result NaN and bitExact false.</summary>
        public static double LocalPrecision(TerrainTileMesh tile, AdtRoot root, out bool bitExact)
        {
            double worst = 0;
            bitExact = true;
            foreach (TerrainChunkMesh ch in tile.Chunks)
            {
                if (ch.State != TerrainChunkState.Built)
                    continue;
                if (ch.Positions == null || ch.Positions.Length != 145 * 3)
                {
                    bitExact = false;
                    return double.NaN;
                }
                AdtRootChunk mc = root.Mcnk[ch.Index];
                for (int k = 0; k < 145; k++)
                {
                    double x, y, ux, uy, uz;
                    TerrainMeshBuilder.VertexWorldXY(tile.TileX, tile.TileY, ch.IndexX, ch.IndexY, k, out x, out y);
                    WorldToUnity.PositionD(x, y, (double)mc.StoredPosition.C + (double)mc.Heights[k], tile.Origin, out ux, out uy, out uz);
                    float sx = ch.Positions[3 * k], sy = ch.Positions[3 * k + 1], sz = ch.Positions[3 * k + 2];
                    double e = Math.Max(Math.Abs(sx - ux), Math.Abs(sz - uz));
                    if (double.IsNaN(e))
                    {
                        bitExact = false;
                        return double.NaN;
                    }
                    worst = Math.Max(worst, e);
                    bitExact &= Bits(sx) == Bits((float)ux) && Bits(sy) == Bits((float)uy) && Bits(sz) == Bits((float)uz);
                }
            }
            return worst;
        }

        /// <summary>The tile's first outer row (uz) and column (ux) are exactly 0 (numerically; -0.0 counts).</summary>
        public static bool FirstRowColumnZero(TerrainTileMesh tile)
        {
            bool ok = true;
            for (int i = 0; i < 16; i++)
            {
                TerrainChunkMesh top = tile.Chunks[i], left = tile.Chunks[i * 16];
                for (int j = 0; j <= 8; j++)
                {
                    if (top.State == TerrainChunkState.Built) ok &= top.Positions[3 * TerrainVertexLayout.Outer(0, j) + 2] == 0f;
                    if (left.State == TerrainChunkState.Built) ok &= left.Positions[3 * TerrainVertexLayout.Outer(j, 0)] == 0f;
                }
            }
            return ok;
        }

        /// <summary>The stored-position generator: f32(f32(32T) - f32((16t + i) f32(C))).</summary>
        public static float StoredGenerator(int tile, int chunk)
        {
            float h32 = (float)(32 * T);
            float c32 = (float)C;
            float step = (float)((double)(16 * tile + chunk) * c32);
            return (float)((double)h32 - step);
        }

        // ================================================================================== tests

        static void CoordinateTests()
        {
            int exact = 0;
            for (int t = 0; t <= 64; t++)
            {
                if (t < 64)
                {
                    WorldOrigin o = WorldToUnity.TileOrigin(t, t);
                    WorldOrigin ox = WorldToUnity.TileOrigin(5, t), oy = WorldToUnity.TileOrigin(t, 9);
                    if (o.X == (32 - t) * T && o.Y == (32 - t) * T && o.Z == 0 && ox.X == (32 - t) * T && oy.Y == (32 - t) * T) exact++;
                }
                else if (TerrainCoordinates.XOfRow(128 * 64) == (32 - 64) * T && TerrainCoordinates.YOfColumn(128 * 64) == (32 - 64) * T)
                    exact++;
            }
            Check(exact == 65, "origin: X0 = (4096 - 128 ty) U equals (32 - ty) T exactly for all 65 ty (and Y0 for tx), Z0 = 0 (" + exact + "/65)");
            WorldOrigin w = WorldToUnity.TileOrigin(32, 50);
            Check(w.X == -18 * T && w.Y == 0 && w.X == TerrainCoordinates.XOfRow(6400), "origin: tile 32_50 is at X0 = -18 T, Y0 = 0");
            Throws<ArgumentOutOfRangeException>(() => WorldToUnity.TileOrigin(64, 0), "origin: tile 64 rejected");
            Throws<ArgumentOutOfRangeException>(() => WorldToUnity.TileOrigin(0, -1), "origin: tile -1 rejected");

            // The single map M(x, y, z) = (-y, z, x) equals the model-space converter on the same inputs.
            float[] samples = { 0f, -0f, 1f, -1f, 0.5f, 123.456f, -9876.5f, 17066.666f, -17066.666f, 3.4e38f, -1.17549435e-38f, 1e-45f };
            bool same = true, dirSame = true;
            int n = 0;
            var zero = new WorldOrigin(0, 0, 0);
            foreach (float x in samples)
                foreach (float y in samples)
                    foreach (float z in samples)
                    {
                        float ax, ay, az, bx, by, bz;
                        WorldToUnity.Position(x, y, z, zero, out ax, out ay, out az);
                        WowCoordinateConverter.ConvertPosition(new WowVec3(x, y, z), out bx, out by, out bz);
                        same &= Bits(ax) == Bits(bx) && Bits(ay) == Bits(by) && Bits(az) == Bits(bz);
                        WorldToUnity.Direction(x, y, z, out ax, out ay, out az);
                        WowCoordinateConverter.ConvertNormal(new WowVec3(x, y, z), out bx, out by, out bz);
                        dirSame &= Bits(ax) == Bits(bx) && Bits(ay) == Bits(by) && Bits(az) == Bits(bz);
                        n++;
                    }
            Check(same, "conversion: WorldToUnity.Position (origin 0) equals WowCoordinateConverter.ConvertPosition bit for bit (" + n + " inputs)");
            Check(dirSame, "conversion: WorldToUnity.Direction equals WowCoordinateConverter.ConvertNormal bit for bit (" + n + " inputs)");
            var org = new WorldOrigin(-9600.0, 533.333, 12.5);
            double dx, dy, dz;
            float fx, fy, fz;
            WorldToUnity.PositionD(-9700.25, 400.5, 80.125, org, out dx, out dy, out dz);
            WorldToUnity.Position(-9700.25, 400.5, 80.125, org, out fx, out fy, out fz);
            Check(dx == -(400.5 - 533.333) && dy == 80.125 - 12.5 && dz == -9700.25 + 9600.0 && fx == (float)dx && fy == (float)dy && fz == (float)dz,
                  "conversion: Position = float cast of PositionD = (-(y - Y0), z - Z0, x - X0)");

            // Vertex x/y by the pinned expressions equal TerrainCoordinates (the parsers' proven layout).
            int[][] tiles = { new[] { 0, 0 }, new[] { 32, 50 }, new[] { 63, 63 }, new[] { 17, 41 }, new[] { 34, 46 } };
            long vertices = 0, bitExact = 0;
            double worst = 0;
            foreach (int[] t in tiles)
                for (int iy = 0; iy < 16; iy++)
                    for (int ix = 0; ix < 16; ix++)
                        for (int k = 0; k < 145; k++)
                        {
                            double x, y, ex, ey;
                            TerrainMeshBuilder.VertexWorldXY(t[0], t[1], ix, iy, k, out x, out y);
                            if (TerrainVertexLayout.IsOuter(k)) TerrainCoordinates.OuterVertex(t[0], t[1], ix, iy, k / 17, k % 17, out ex, out ey);
                            else TerrainCoordinates.InnerVertex(t[0], t[1], ix, iy, k / 17, k % 17 - 9, out ex, out ey);
                            worst = Math.Max(worst, Math.Max(Math.Abs(x - ex), Math.Abs(y - ey)));
                            if (x == ex && y == ey) bitExact++;
                            vertices++;
                        }
            Check(worst <= 1e-9, string.Format("vertices: VertexWorldXY within 1e-9 yd of TerrainCoordinates.OuterVertex/InnerVertex ({0} vertices on 5 tiles, max {1}, {2} bit-exact)",
                                               vertices, G(worst), bitExact));
            Throws<ArgumentOutOfRangeException>(() => { double a, b; TerrainMeshBuilder.VertexWorldXY(0, 0, 0, 0, 145, out a, out b); }, "vertices: MCVT entry 145 rejected");
            Throws<ArgumentOutOfRangeException>(() => { double a, b; TerrainMeshBuilder.VertexWorldXY(0, 0, 16, 0, 0, out a, out b); }, "vertices: chunk 16 rejected");

            // Identity tolerance: the float32 generator over all 64 x 16 rows and columns.
            double gmax = 0;
            int rows = 0, within = 0;
            for (int t = 0; t < 64; t++)
                for (int i = 0; i < 16; i++)
                {
                    double exactX = TerrainCoordinates.XOfRow(128 * t + 8 * i);
                    double d = Math.Abs(StoredGenerator(t, i) - exactX);
                    gmax = Math.Max(gmax, d);
                    rows++;
                    if (d <= TerrainMeshBuilder.IdentityTolerance) within++;
                }
            Check(rows == 1024 && within == 1024 && Math.Abs(gmax - 1.0 / 512) < 1e-9,
                  "identity: the stored-position generator f32(f32(32T) - f32((16t+i) f32(C))) stays within 0.01 yd over all 1,024 rows/columns, max " + G(gmax) + " = 1/512");
            Check(TerrainMeshBuilder.IdentityTolerance >= 5 * gmax && TerrainMeshBuilder.IdentityTolerance < C / 1000,
                  "identity: the tolerance is >= 5x the generator's worst case and far below one chunk (C = 33.3 yd)");
        }

        static void CellTableTests()
        {
            var got = new int[12];
            bool ok = true;
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++)
                {
                    TerrainMeshBuilder.CellIndices(r, c, got);
                    int a = TerrainVertexLayout.Outer(r, c), b = TerrainVertexLayout.Outer(r, c + 1), cc = TerrainVertexLayout.Outer(r + 1, c),
                        d = TerrainVertexLayout.Outer(r + 1, c + 1), m = TerrainVertexLayout.Inner(r, c);
                    int[] want = { m, a, b, m, b, d, m, d, cc, m, cc, a };
                    for (int i = 0; i < 12; i++) ok &= got[i] == want[i];
                }
            Check(ok, "cells: CellIndices is (m,a,b), (m,b,d), (m,d,cc), (m,cc,a) for all 64 cells");
            Throws<ArgumentOutOfRangeException>(() => TerrainMeshBuilder.CellIndices(8, 0, new int[12]), "cells: row 8 rejected");
            Throws<ArgumentException>(() => TerrainMeshBuilder.CellIndices(0, 0, new int[11]), "cells: an 11-slot buffer rejected");
        }

        static void FlatTileTests()
        {
            byte[] bytes = FlatRoot(32, 50, 50f);
            AdtRoot root = AdtParser.ParseRoot(bytes, 32, 50);
            TerrainTileMesh t = TerrainMeshBuilder.Build(root, 32, 50, WorldToUnity.TileOrigin(32, 50));
            Check(t.Chunks.Length == 256 && t.ChunksBuilt == 256 && t.ChunksSkipped == 0 && t.ChunksUnsupported == 0 && t.Vertices == 37120 &&
                  t.Triangles == 65536 && t.HoleCells == 0, string.Format("flat: 256 chunks built, 37,120 vertices, 65,536 triangles, 0 hole cells (got {0}, {1}, {2}, {3})",
                                                                           t.ChunksBuilt, t.Vertices, t.Triangles, t.HoleCells));
            Check(t.MeshBytes == 256L * (145 * 24 + 768 * 2), "flat: mesh bytes = 256 x (145 x 24 + 768 x 2) = 1,284,096 (got " + t.MeshBytes + ")");
            bool order = true;
            for (int n = 0; n < 256; n++)
                order &= t.Chunks[n].Index == n && t.Chunks[n].IndexX == n % 16 && t.Chunks[n].IndexY == n / 16 && t.Chunks[n].Positions.Length == 435 &&
                         t.Chunks[n].Normals.Length == 435 && t.Chunks[n].Indices.Length == 768 && t.Chunks[n].Reason == null;
            Check(order, "flat: chunks in file order at (n % 16, n / 16), 435 position and normal floats, 768 indices each");
            bool up = true;
            foreach (TerrainChunkMesh ch in t.Chunks)
                for (int k = 0; k < 145; k++)
                    up &= ch.Normals[3 * k] == 0f && ch.Normals[3 * k + 1] == 1f && ch.Normals[3 * k + 2] == 0f && ch.Positions[3 * k + 1] == 50f;
            Check(up, "flat: every vertex at height 50 with normal exactly (0, 1, 0)");
            Check(t.BoundsMin[0] == 0f && t.BoundsMax[2] == 0f && t.BoundsMin[1] == 50f && t.BoundsMax[1] == 50f &&
                  Math.Abs(t.BoundsMax[0] - T) < 1e-3 && Math.Abs(t.BoundsMin[2] + T) < 1e-3,
                  string.Format("flat: tile bounds x 0..T, y 50, z -T..0 (east = +X, north = +Z) ({0}, {1}, {2})..({3}, {4}, {5})",
                                t.BoundsMin[0], t.BoundsMin[1], t.BoundsMin[2], t.BoundsMax[0], t.BoundsMax[1], t.BoundsMax[2]));
            TerrainChunkMesh se = t.Chunks[255];
            Check(se.Positions[3 * 144] > se.Positions[0] && se.Positions[3 * 144 + 2] < se.Positions[2] && t.Chunks[1].Positions[0] > t.Chunks[0].Positions[0] &&
                  t.Chunks[16].Positions[2] < t.Chunks[0].Positions[2],
                  "flat: chunk index x grows east (+X) and index y south (-Z); column c east, row r south");
            int tris;
            double cy;
            string p = CellStructureProblem(t, out tris, out cy);
            Check(p == null && tris == 65536, "flat: cell structure (inner index first, 5 cell vertices, Cross.y = U^2/2, areas sum to U^2, no duplicates) " + (p ?? "ok"));
            Check(FirstRowColumnZero(t), "local: the tile's first outer row (z) and column (x) are exactly 0");
            Check(t.Chunks[0].Positions[0] == 0f && t.Chunks[0].Positions[2] == 0f, "local: the north-west corner vertex is at x = z = 0");
            int points;
            p = BorderProblem(t, out points);
            Check(p == null && points == 2 * 15 * 16 * 9, "flat: x/z and normals bit-equal at all " + points + " chunk-border copies " + (p ?? ""));

            foreach (int[] tl in new[] { new[] { 0, 0 }, new[] { 32, 50 }, new[] { 63, 63 } })
            {
                AdtRoot r = AdtParser.ParseRoot(FlatRoot(tl[0], tl[1]), tl[0], tl[1]);
                TerrainTileMesh m = TerrainMeshBuilder.Build(r, tl[0], tl[1], WorldToUnity.TileOrigin(tl[0], tl[1]));
                bool exact;
                double e = LocalPrecision(m, r, out exact);
                Check(e <= 2.1e-5 && exact && FirstRowColumnZero(m),
                      string.Format("precision: tile {0}_{1}: stored x/z within {2} yd <= 2.1e-5 of the double local, every stored x/y/z the float cast of it bit for bit, first row/column 0",
                                    tl[0], tl[1], G(e)));
            }
            // For the record, the justification of origin-local vertices: the same tile built with its origin at the
            // map centre (absolute coordinates), measured on what the builder stored there.
            AdtRoot edge = AdtParser.ParseRoot(FlatRoot(0, 0), 0, 0);
            TerrainTileMesh abs = TerrainMeshBuilder.Build(edge, 0, 0, new WorldOrigin(0, 0, 0));
            bool absExact;
            double ae = LocalPrecision(abs, edge, out absExact);
            Check(ae > 6e-4 && absExact, "precision: tile 0_0 built in absolute coordinates (origin 0, 0, 0) stores floats " + G(ae) + " yd off the double position (why vertices are origin-local)");
        }

        static void HoleTests()
        {
            // Chunk n (0..63) has exactly hole bit n; chunks 64..255 none.
            AdtRoot root = AdtParser.ParseRoot(RootWith(31, 48, n => new RootChunkSpec { Holes = n < 64 ? 1UL << n : 0UL, Height = k => (float)Math.Sin(k + n) }), 31, 48);
            TerrainTileMesh t = TerrainMeshBuilder.Build(root, 31, 48, WorldToUnity.TileOrigin(31, 48));
            var full = new List<ushort>();
            var cell = new int[12];
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++)
                {
                    TerrainMeshBuilder.CellIndices(r, c, cell);
                    foreach (int i in cell) full.Add((ushort)i);
                }
            int exact = 0;
            for (int bit = 0; bit < 64; bit++)
            {
                TerrainChunkMesh ch = t.Chunks[bit];
                int inner = TerrainVertexLayout.Inner(bit / 8, bit % 8);
                // Expected: the full list minus the 4 triangles whose first index is this cell's inner vertex.
                var want = new List<ushort>();
                int removed = 0;
                for (int q = 0; q < full.Count; q += 3)
                {
                    if (full[q] == inner) { removed++; continue; }
                    want.Add(full[q]); want.Add(full[q + 1]); want.Add(full[q + 2]);
                }
                bool same = ch.Indices.Length == want.Count && removed == 4;
                for (int i = 0; same && i < want.Count; i++) same = ch.Indices[i] == want[i];
                bool innerUnused = Array.IndexOf(ch.Indices, (ushort)inner) < 0;
                bool innerDefault = ch.Normals[3 * inner] == 0f && ch.Normals[3 * inner + 1] == 1f && ch.Normals[3 * inner + 2] == 0f;
                if (same && innerUnused && innerDefault && ch.Triangles == 252 && ch.HoleCells == 1 && ch.HoleMask == 1UL << bit)
                    exact++;
            }
            Check(exact == 64, "holes: each single bit 0..63 removes exactly the 4 triangles whose first index is Inner(bit/8, bit%8) and nothing else; " +
                               "that inner vertex gets (0, 1, 0) (" + exact + "/64 bits)");
            Check(t.Triangles == 65536 - 64 * 4 && t.HoleCells == 64 && t.Chunks[64].Triangles == 256, "holes: tile totals 65,280 triangles, 64 hole cells");
            Check(new TerrainHoles { Kind = HoleMaskKind.HighResolution, HighResolutionBits = 1UL << 21 }.IsHole(2, 5) == true && t.Chunks[21].HoleMask == 1UL << 21 &&
                  Array.IndexOf(t.Chunks[21].Indices, (ushort)TerrainVertexLayout.Inner(2, 5)) < 0, "holes: bit 21 is cell (2, 5): row 2, column 5, not transposed");

            // Full mask on one chunk; then on every chunk.
            AdtRoot oneFull = AdtParser.ParseRoot(RootWith(31, 48, n => new RootChunkSpec { Holes = n == 37 ? ulong.MaxValue : 0UL }), 31, 48);
            TerrainTileMesh of = TerrainMeshBuilder.Build(oneFull, 31, 48, WorldToUnity.TileOrigin(31, 48));
            TerrainChunkMesh f37 = of.Chunks[37];
            Check(f37.State == TerrainChunkState.Built && f37.Triangles == 0 && f37.Indices.Length == 0 && f37.BoundsMin == null && f37.BoundsMax == null &&
                  f37.HoleCells == 64 && f37.Positions != null && of.Triangles == 65536 - 256 && of.Vertices == 255 * 145 &&
                  of.MeshBytes == 255L * (145 * 24 + 768 * 2), "holes: a full mask leaves a Built chunk with 0 triangles, no indices, no bounds; it uploads nothing");
            AdtRoot allFull = AdtParser.ParseRoot(RootWith(31, 48, n => new RootChunkSpec { Holes = ulong.MaxValue }), 31, 48);
            TerrainTileMesh af = TerrainMeshBuilder.Build(allFull, 31, 48, WorldToUnity.TileOrigin(31, 48));
            bool allDefault = true;
            foreach (TerrainChunkMesh ch in af.Chunks)
                for (int i = 0; i < 435; i += 3) allDefault &= ch.Normals[i] == 0f && ch.Normals[i + 1] == 1f && ch.Normals[i + 2] == 0f;
            Check(af.Triangles == 0 && af.BoundsMin == null && af.BoundsMax == null && af.Vertices == 0 && af.MeshBytes == 0 && af.ChunksBuilt == 256 &&
                  af.HoleCells == 256 * 64 && allDefault, "holes: an all-hole tile has 0 triangles, no bounds, no mesh bytes; every vertex normal is (0, 1, 0)");

            // Bounds from referenced vertices only: hole cell (3, 3)'s inner vertex is 1000 yd down.
            int hidden = TerrainVertexLayout.Inner(3, 3);
            AdtRoot deep = AdtParser.ParseRoot(RootWith(31, 48, n => new RootChunkSpec
            {
                Holes = n == 90 ? 1UL << 27 : 0UL, Height = k => n == 90 && k == hidden ? -1000f : 0f,
            }), 31, 48);
            TerrainTileMesh dt = TerrainMeshBuilder.Build(deep, 31, 48, WorldToUnity.TileOrigin(31, 48));
            Check(dt.Chunks[90].Positions[3 * hidden + 1] == -950f && dt.Chunks[90].BoundsMin[1] == 50f && dt.BoundsMin[1] == 50f,
                  "bounds: an unreferenced hole-cell vertex (at -950) stays out of the chunk and tile bounds (min y 50)");
            int tris;
            double cy;
            string p = CellStructureProblem(t, out tris, out cy);
            Check(p == null && tris == 65280, "holes: cell structure intact around every hole " + (p ?? "ok"));
        }

        static void RampTests()
        {
            const float baseZ = 100f;
            // Rising toward world +x (north): must rise toward Unity +Z.
            AdtRoot rx = AdtParser.ParseRoot(RampRoot(30, 47, 0.25, 0, baseZ), 30, 47);
            TerrainTileMesh tx = TerrainMeshBuilder.Build(rx, 30, 47, WorldToUnity.TileOrigin(30, 47));
            // Rising toward world -y (east): must rise toward Unity +X.
            AdtRoot ry = AdtParser.ParseRoot(RampRoot(30, 47, 0, -0.25, baseZ), 30, 47);
            TerrainTileMesh ty = TerrainMeshBuilder.Build(ry, 30, 47, WorldToUnity.TileOrigin(30, 47));
            double ex = 0, ey = 0;
            foreach (TerrainChunkMesh ch in tx.Chunks)
                for (int k = 0; k < 145; k++)
                    ex = Math.Max(ex, Math.Abs(ch.Positions[3 * k + 1] - (baseZ + 0.25 * ch.Positions[3 * k + 2])));
            foreach (TerrainChunkMesh ch in ty.Chunks)
                for (int k = 0; k < 145; k++)
                    ey = Math.Max(ey, Math.Abs(ch.Positions[3 * k + 1] - (baseZ + 0.25 * ch.Positions[3 * k])));
            Check(ex < 1e-3 && tx.BoundsMax[1] > tx.BoundsMin[1] + 100 && tx.Chunks[0].Positions[1] > tx.Chunks[240].Positions[1],
                  "orientation: a ramp rising toward world +x rises toward Unity +Z: y = 100 + 0.25 z (max error " + G(ex) + ")");
            Check(ey < 1e-3 && ty.Chunks[15].Positions[3 * 8 + 1] > ty.Chunks[0].Positions[1],
                  "orientation: a ramp rising toward world -y rises toward Unity +X: y = 100 + 0.25 x (max error " + G(ey) + ")");
            double nzErr = 0, nxErr = 0;
            double l = Math.Sqrt(1 + 0.0625);
            foreach (TerrainChunkMesh ch in tx.Chunks)
                for (int k = 0; k < 145; k++)
                    nzErr = Math.Max(nzErr, Math.Max(Math.Abs(ch.Normals[3 * k]), Math.Max(Math.Abs(ch.Normals[3 * k + 1] - 1 / l), Math.Abs(ch.Normals[3 * k + 2] + 0.25 / l))));
            foreach (TerrainChunkMesh ch in ty.Chunks)
                for (int k = 0; k < 145; k++)
                    nxErr = Math.Max(nxErr, Math.Max(Math.Abs(ch.Normals[3 * k] + 0.25 / l), Math.Max(Math.Abs(ch.Normals[3 * k + 1] - 1 / l), Math.Abs(ch.Normals[3 * k + 2]))));
            Check(nzErr < 1e-5 && nxErr < 1e-5, string.Format("orientation: ramp normals lean downhill: (0, 1, -0.25)/|.| and (-0.25, 1, 0)/|.| (max errors {0}, {1})", G(nzErr), G(nxErr)));

            // The trap, pinned: a world-frame cross product in mesh order, converted as a direction, points DOWN.
            int down = 0, up = 0, total = 0;
            var cell = new int[12];
            foreach (TerrainChunkMesh ch in tx.Chunks)
            {
                AdtRootChunk mc = rx.Mcnk[ch.Index];
                for (int t = 0; t < ch.Indices.Length; t += 3)
                {
                    var w = new double[3][];
                    for (int v = 0; v < 3; v++)
                    {
                        int k = ch.Indices[t + v];
                        double x, y;
                        TerrainMeshBuilder.VertexWorldXY(30, 47, ch.IndexX, ch.IndexY, k, out x, out y);
                        w[v] = new[] { x, y, (double)mc.StoredPosition.C + mc.Heights[k] };
                    }
                    double e1x = w[1][0] - w[0][0], e1y = w[1][1] - w[0][1], e1z = w[1][2] - w[0][2];
                    double e2x = w[2][0] - w[0][0], e2y = w[2][1] - w[0][1], e2z = w[2][2] - w[0][2];
                    float dx, dy, dz;
                    WorldToUnity.Direction(e1y * e2z - e1z * e2y, e1z * e2x - e1x * e2z, e1x * e2y - e1y * e2x, out dx, out dy, out dz);
                    if (dy < 0) down++;
                    if (CrossY(ch.Positions, ch.Indices[t], ch.Indices[t + 1], ch.Indices[t + 2]) > 0) up++;
                    total++;
                }
            }
            Check(total == 65536 && down == total && up == total,
                  "winding trap: Direction(world cross in mesh order) points DOWN for all " + down + " triangles (det M = -1) while the Unity-space cross points up for " + up);

            int tris;
            double cy;
            string p = CellStructureProblem(tx, out tris, out cy);
            Check(p == null && tris == 65536, "ramp: cell structure and |Cross.y - U^2/2| <= 1e-3 on a sloped tile (max " + G(cy) + ") " + (p ?? "ok"));
        }

        /// <summary>A bumpy tile whose heights follow one world height field, so shared seam vertices get the
        /// same height (as in real data) and normals vary.</summary>
        public static byte[] BumpyRoot(int tileX, int tileY, Func<int, RootChunkSpec> tweak = null)
        {
            Func<double, double, double> z = (x, y) => 60 + 20 * Math.Sin(x / 37.0) + 15 * Math.Cos(y / 23.0) + 0.05 * x + 3 * Math.Sin((x + y) / 7.0);
            return RootWith(tileX, tileY, n =>
            {
                RootChunkSpec s = tweak != null ? tweak(n) : null;
                s = s ?? new RootChunkSpec();
                if (s.Height == null)
                    s.Height = WorldHeights(tileX, tileY, n, z, s.BaseHeight);
                return s;
            });
        }

        /// <summary>A root whose chunks 9..12 carry an MCNR or MCCV the parser calls malformed (MCNR of 400 bytes,
        /// MCCV of 100 bytes, MCNR twice, MCCV twice) and chunks 13..15 well-formed variants (no MCNR, an MCNR of
        /// exactly 435 bytes, an MCCV of 580 bytes); the rest are default chunks. Skipped are 9..12 only.
        /// TerrainCorpusTests dumps it for the Python reference, which applies the same rule.</summary>
        public static byte[] SideChunkRoot(int tileX, int tileY)
        {
            var heights = new byte[580];
            for (int k = 0; k < 145; k++) PutF32(heights, 4 * k, k * 0.25f);
            byte[] v = Chunk("MCVT", heights), nr = Chunk("MCNR", new byte[448]), cv = Chunk("MCCV", new byte[580]);
            return Root(tileX, tileY, n =>
                n == 9 ? RootMcnkOnly(n, tileX, tileY, v, Chunk("MCNR", new byte[400]))
                : n == 10 ? RootMcnkOnly(n, tileX, tileY, v, Chunk("MCCV", new byte[100]), nr)
                : n == 11 ? RootMcnkOnly(n, tileX, tileY, v, nr, nr)
                : n == 12 ? RootMcnkOnly(n, tileX, tileY, v, cv, cv, nr)
                : n == 13 ? RootMcnkOnly(n, tileX, tileY, v)
                : n == 14 ? RootMcnkOnly(n, tileX, tileY, v, Chunk("MCNR", new byte[435]))
                : n == 15 ? RootMcnkOnly(n, tileX, tileY, v, cv, nr)
                : null);
        }

        static void NormalTests()
        {
            AdtRoot root = AdtParser.ParseRoot(BumpyRoot(33, 49, n => new RootChunkSpec { Holes = n == 100 ? 0xFF00FF00FF00FF00UL : n == 17 ? ulong.MaxValue : 0UL }), 33, 49);
            TerrainTileMesh t = TerrainMeshBuilder.Build(root, 33, 49, WorldToUnity.TileOrigin(33, 49));
            int compared, defaults, points;
            double err = NormalError(t, root, out compared, out defaults);
            Check(err <= 1e-6 && compared == 256 * 145, string.Format("normals: equal an independent tile-wide recomputation within 1e-6 at all {0} vertices (max {1}; {2} without a kept triangle)",
                                                                      compared, G(err), defaults));
            string p = BorderProblem(t, out points);
            Check(p == null && points == 2 * 15 * 16 * 9, "normals: bit-equal at every chunk-border copy on a bumpy tile with holes (" + points + " points) " + (p ?? ""));
            double minUp = 1;
            foreach (TerrainChunkMesh ch in t.Chunks)
                for (int k = 0; k < 145; k++) minUp = Math.Min(minUp, ch.Normals[3 * k + 1]);
            Check(minUp > 0, "normals: every normal of the bumpy tile points up (min y " + G(minUp) + ")");

            // Tile-wide versus per-chunk: a chunk-border vertex sees its neighbour's triangles.
            TerrainChunkMesh a = t.Chunks[5 * 16 + 5], b = t.Chunks[5 * 16 + 6];
            int k8 = TerrainVertexLayout.Outer(4, 8);
            Check(Bits(a.Normals[3 * k8]) == Bits(b.Normals[3 * TerrainVertexLayout.Outer(4, 0)]) && a.Normals[3 * k8 + 1] < 1f,
                  "normals: accumulated across the chunk border (a sloped border vertex has one normal in both chunks)");

            int tris;
            double cy;
            p = CellStructureProblem(t, out tris, out cy);
            Check(p == null, "normals: bumpy tile cell structure, max |Cross.y - U^2/2| " + G(cy) + " " + (p ?? "ok"));
        }

        static void FailureTests()
        {
            // Tile identity.
            byte[] r3047 = FlatRoot(30, 47);
            Throws<TerrainTileMismatchException>(() => BuildBytes(r3047, 31, 47), "identity: a root of 30_47 built as 31_47 fails the whole build", "root ADT does not belong to tile 31_47");
            Throws<TerrainTileMismatchException>(() => BuildBytes(r3047, 30, 46), "identity: a root of 30_47 built as 30_46 fails the whole build", "root ADT does not belong to tile 30_46");
            Throws<TerrainTileMismatchException>(() => BuildBytes(RootWith(30, 47, n => new RootChunkSpec { StoredXOffset = n == 200 ? C : 0 }), 30, 47),
                                                 "identity: one chunk stored a chunk away (33.3 yd) fails the whole build", "MCNK 200");
            Throws<TerrainTileMismatchException>(() => BuildBytes(RootWith(30, 47, n => new RootChunkSpec { StoredXOffset = n == 3 ? 0.011 : 0 }), 30, 47),
                                                 "identity: stored x 0.011 yd off fails", "MCNK 3");
            Throws<TerrainTileMismatchException>(() => BuildBytes(RootWith(30, 47, n => new RootChunkSpec { StoredYOffset = n == 3 ? -0.011 : 0 }), 30, 47),
                                                 "identity: stored y 0.011 yd off fails", "MCNK 3");
            TerrainTileMesh near = BuildBytes(RootWith(30, 47, n => new RootChunkSpec { StoredXOffset = 0.009, StoredYOffset = -0.009 }), 30, 47);
            Check(near.ChunksBuilt == 256, "identity: stored x/y 0.009 yd off (inside the tolerance) still builds");
            try
            {
                BuildBytes(RootWith(30, 47, n => new RootChunkSpec { StoredTileX = 29 }), 30, 47);
                Check(false, "identity: the exception names chunk, stored and expected positions");
            }
            catch (TerrainTileMismatchException e)
            {
                Check(e.ChunkIndex == 0 && e.TileX == 30 && e.TileY == 47 && Math.Abs(e.StoredY - e.ExpectedY - T) < 0.01 && e is TerrainBuildException,
                      "identity: the exception names chunk, stored and expected positions (" + e.Message + ")");
            }

            // No decodable chunk.
            Throws<TerrainBuildException>(() => BuildBytes(Root(30, 47, n => ShortMcnk()), 30, 47), "build: no decodable chunk fails the build", "no decodable chunk");
            TerrainTileMesh one = BuildBytes(Root(30, 47, n => n == 77 ? null : ShortMcnk()), 30, 47);
            Check(one.ChunksBuilt == 1 && one.ChunksSkipped == 255 && one.Triangles == 256 && one.Chunks[77].BoundsMin != null,
                  "build: one decodable chunk among 255 malformed builds that chunk alone");

            // Heights out of range.
            TerrainTileMesh tall = BuildBytes(RootWith(30, 47, n => new RootChunkSpec
            {
                Height = k => n == 7 && k == 5 ? 1e30f : n == 8 && k == 0 ? 70000f : n == 9 && k == 144 ? 65400f : 0f,
                BaseHeight = n == 10 ? 1e30f : n == 11 ? -65537f : 100f,
            }), 30, 47);
            Check(tall.Chunks[7].State == TerrainChunkState.Skipped && tall.Chunks[7].Reason.StartsWith("height out of range", StringComparison.Ordinal) &&
                  tall.Chunks[7].Positions == null && tall.Chunks[7].Triangles == 0,
                  "heights: a vertex at 1e30 yd skips the chunk 'height out of range' (" + tall.Chunks[7].Reason + ")");
            Check(tall.Chunks[8].State == TerrainChunkState.Skipped && tall.Chunks[10].State == TerrainChunkState.Skipped &&
                  tall.Chunks[11].State == TerrainChunkState.Skipped && tall.Chunks[10].Reason.StartsWith("height out of range", StringComparison.Ordinal),
                  "heights: |C + h| = 70,100, base 1e30 and base -65,537 are skipped too");
            Check(tall.Chunks[9].State == TerrainChunkState.Built && tall.ChunksBuilt == 252 && tall.ChunksSkipped == 4 && tall.Triangles == 252 * 256,
                  "heights: 65,500 yd is inside the range; the other 252 chunks build");

            // Hole mask that cannot be read.
            TerrainTileMesh low = BuildBytes(RootWith(30, 47, n => n == 12 ? new RootChunkSpec { Flags = 0, LowResolutionHoles = 0x8001 }
                                                                  : n == 13 ? new RootChunkSpec { Flags = 0 } : null), 30, 47);
            Check(low.Chunks[12].State == TerrainChunkState.Unsupported && low.Chunks[12].Triangles == 0 && low.Chunks[12].Positions == null &&
                  low.Chunks[12].Reason.Contains("hole mask") && low.ChunksUnsupported == 1,
                  "holes: IsHole null (non-zero low-resolution mask) makes the chunk Unsupported (" + low.Chunks[12].Reason + ")");
            Check(low.Chunks[13].State == TerrainChunkState.Built && low.Chunks[13].Triangles == 256,
                  "holes: a zero low-resolution field (flag 0x10000 clear) builds with no hole");

            // Malformed chunks are skipped, never placed at z = 0; the rest builds.
            TerrainTileMesh bad = BuildBytes(Root(30, 47, n =>
                n == 9 ? ShortMcnk()
                : n == 0 ? RootMcnk(0, 30, 47, new RootChunkSpec { McvtBytes = 576 })
                : n == 2 ? RootMcnk(2, 30, 47, new RootChunkSpec { StoredX = float.NaN })
                : n == 3 ? RootMcnk(3, 30, 47, new RootChunkSpec { Height = k => k == 7 ? float.NaN : 1f })
                : n == 4 ? RootMcnk(4, 30, 47, new RootChunkSpec { HeaderIndexY = 9 })
                : n == 5 ? RootMcnk(5, 30, 47, new RootChunkSpec { McvtBytes = -1 })
                : null), 30, 47);
            bool skipped = true;
            foreach (int n in new[] { 0, 2, 3, 4, 5, 9 })
                skipped &= bad.Chunks[n].State == TerrainChunkState.Skipped && bad.Chunks[n].Positions == null && bad.Chunks[n].Normals == null &&
                           bad.Chunks[n].Triangles == 0 && bad.Chunks[n].Reason != null && bad.Chunks[n].Reason.StartsWith("MCNK " + n + " is", StringComparison.Ordinal);
            Check(skipped && bad.ChunksSkipped == 6 && bad.ChunksBuilt == 250 && bad.Triangles == 250 * 256,
                  "malformed: short MCNK, MCVT of 576 bytes, NaN stored x, NaN height, wrong header index, no MCVT -> Skipped with the reason, the rest builds");
            Check(bad.Chunks[9].Reason.Contains("smaller than its 128-byte header") && bad.Chunks[2].Reason.Contains("stored position"),
                  "malformed: the reason quotes the parser's issue (" + bad.Chunks[9].Reason + " | " + bad.Chunks[2].Reason + ")");
            int points;
            string bp = BorderProblem(bad, out points);
            Check(bp == null, "malformed: border normals of the chunks around a skipped one stay consistent " + (bp ?? ""));

            // MCNR and MCCV are not used by the mesh, but the parser marks a short or repeated MCNR and an MCCV that is
            // not 580 bytes or repeated Malformed, so the chunk is Skipped; a missing MCNR is only Unexpected. The
            // Python reference (terrain_reference.py) skips exactly the same chunks.
            TerrainTileMesh side = BuildBytes(SideChunkRoot(32, 50), 32, 50);
            bool sideSkipped = true;
            foreach (int n in new[] { 9, 10, 11, 12 })
                sideSkipped &= side.Chunks[n].State == TerrainChunkState.Skipped && side.Chunks[n].Positions == null && side.Chunks[n].Triangles == 0 &&
                               side.Chunks[n].Reason != null && side.Chunks[n].Reason.StartsWith("MCNK " + n + " is malformed: ", StringComparison.Ordinal);
            Check(sideSkipped && side.Chunks[9].Reason.Contains("fewer than 435") && side.Chunks[10].Reason.Contains("expected 580") &&
                  side.Chunks[11].Reason.Contains("more than once") && side.Chunks[12].Reason.Contains("more than once"),
                  "malformed: MCNR of 400 bytes, MCCV of 100 bytes, MCNR twice, MCCV twice -> Skipped (" + side.Chunks[9].Reason + " | " + side.Chunks[10].Reason + ")");
            Check(side.Chunks[13].State == TerrainChunkState.Built && side.Chunks[14].State == TerrainChunkState.Built && side.Chunks[15].State == TerrainChunkState.Built &&
                  side.ChunksBuilt == 252 && side.ChunksSkipped == 4,
                  "malformed: no MCNR, an MCNR of exactly 435 bytes and one MCCV of 580 bytes build; 252 built, 4 skipped");

            // Input rejection.
            AdtRoot ok = AdtParser.ParseRoot(r3047, 30, 47);
            WorldOrigin o = WorldToUnity.TileOrigin(30, 47);
            Throws<ArgumentOutOfRangeException>(() => TerrainMeshBuilder.Build(ok, 64, 47, o), "input: tile x 64 rejected");
            Throws<ArgumentOutOfRangeException>(() => TerrainMeshBuilder.Build(ok, -1, 47, o), "input: tile x -1 rejected");
            Throws<ArgumentOutOfRangeException>(() => TerrainMeshBuilder.Build(ok, 30, 64, o), "input: tile y 64 rejected");
            Throws<ArgumentNullException>(() => TerrainMeshBuilder.Build(null, 30, 47, o), "input: null root rejected");
            var short255 = new AdtRoot { Mcnk = new AdtRootChunk[255] };
            Throws<ArgumentException>(() => TerrainMeshBuilder.Build(short255, 30, 47, o), "input: a root with 255 MCNKs rejected", "255");
            Throws<ArgumentException>(() => TerrainMeshBuilder.Build(new AdtRoot(), 30, 47, o), "input: a root without MCNKs rejected");
            var holey = new AdtRoot { Mcnk = (AdtRootChunk[])ok.Mcnk.Clone(), Issues = ok.Issues };
            holey.Mcnk[40] = null;
            TerrainTileMesh hm = TerrainMeshBuilder.Build(holey, 30, 47, o);
            Check(hm.Chunks[40].State == TerrainChunkState.Skipped && hm.ChunksBuilt == 255, "input: a missing MCNK object is skipped, not dereferenced");
            AdtRoot shortNine = AdtParser.ParseRoot(Root(30, 47, n => n == 9 ? ShortMcnk() : null), 30, 47);
            TerrainTileMesh ni = TerrainMeshBuilder.Build(new AdtRoot { Mcnk = shortNine.Mcnk, Issues = null }, 30, 47, o);
            Check(ni.Chunks[9].State == TerrainChunkState.Skipped && ni.Chunks[9].Reason == "MCNK 9 is Malformed (see the root's issues)" && ni.ChunksBuilt == 255,
                  "input: a root without an issue list still skips and explains a malformed chunk (" + ni.Chunks[9].Reason + ")");
        }

        static void ThreadTests()
        {
            // No mutable static state anywhere in the world layer (types of Wmv.Wow.World in this assembly).
            var bad = new List<string>();
            foreach (Type type in typeof(TerrainMeshBuilder).Assembly.GetTypes())
            {
                if (type.Namespace != "Wmv.Wow.World")
                    continue;
                foreach (FieldInfo f in type.GetFields(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
                    if (!f.IsLiteral && !f.IsInitOnly && !f.Name.Contains("<"))
                        bad.Add(type.Name + "." + f.Name);
            }
            Check(bad.Count == 0, "threads: no mutable static field in Wmv.Wow.World " + (bad.Count > 0 ? string.Join(", ", bad) : ""));

            byte[] bytes = BumpyRoot(30, 47, n => new RootChunkSpec { Holes = (ulong)n * 0x9E3779B97F4A7C15UL });
            var results = new TerrainTileMesh[4];
            var threads = new Thread[4];
            for (int i = 0; i < 4; i++)
            {
                int slot = i;
                threads[i] = new Thread(() => { for (int rep = 0; rep < 3; rep++) results[slot] = BuildBytes(bytes, 30, 47); });
            }
            foreach (Thread th in threads) th.Start();
            foreach (Thread th in threads) th.Join();
            string a = TerrainDump.ToText(TerrainDump.FromMesh(results[0], "runner"));
            bool same = true;
            for (int i = 1; i < 4; i++) same &= TerrainDump.ToText(TerrainDump.FromMesh(results[i], "runner")) == a;
            Check(same && results[0].Triangles < 65536, "threads: four concurrent builds of one root give identical dumps");
        }

        static void DumpTests()
        {
            TerrainTileMesh t = BuildBytes(RootWith(32, 50, n => new RootChunkSpec
            {
                Holes = n == 3 ? 0xC0C0UL : n == 4 ? ulong.MaxValue : 0UL,
                Flags = n == 6 ? 0u : 0x10000u, LowResolutionHoles = (ushort)(n == 6 ? 1 : 0),
            }), 32, 50);
            TerrainDumpTile d = TerrainDump.FromMesh(t, "runner");
            d.Meta.Add(new KeyValuePair<string, string>("client", "synthetic"));
            d.Meta.Add(new KeyValuePair<string, string>("note", "two words\nand a line break \u00e9"));
            string text = TerrainDump.ToText(d);
            string[] lines = text.Split('\n');
            bool ascii = true;
            foreach (char ch in text) ascii &= ch == '\n' || (ch >= 32 && ch <= 126);
            Check(ascii && !text.Contains("\r") && text.EndsWith("end\n", StringComparison.Ordinal), "dump: ASCII only, LF line ends, ends with 'end'");
            Check(lines[0] == "wmv-terrain-dump 1" && lines[1] == "source runner" && lines[2] == "tile 32 50" &&
                  lines[4] == "counts 256 255 0 1 36830 65008 68" && lines[6] == "meshbytes " + t.MeshBytes,
                  "dump: header records (" + lines[4] + ")");
            Check(lines[3] == "origin c0c2c00000000000 0000000000000000 0000000000000000", "dump: origin as double bits, X0 = -18 T = -9600 (" + lines[3] + ")");
            Check(Array.IndexOf(lines, "meta note two words and a line break ?") > 0 && Array.IndexOf(lines, "meta client synthetic") > 0,
                  "dump: meta values are one printable-ASCII line");
            int chunkLines = 0, posLines = 0, idxEmpty = 0, reasons = 0;
            bool posOk = true;
            foreach (string l in lines)
            {
                if (l.StartsWith("chunk ", StringComparison.Ordinal)) chunkLines++;
                if (l.StartsWith("reason ", StringComparison.Ordinal)) reasons++;
                if (l == "idx") idxEmpty++;
                if (l.StartsWith("pos ", StringComparison.Ordinal))
                {
                    posLines++;
                    string[] f = l.Split(' ');
                    posOk &= f.Length == 436;
                    for (int i = 1; i < f.Length; i++) posOk &= f[i].Length == 8;
                }
            }
            Check(chunkLines == 256 && posLines == 255 && idxEmpty == 1 && reasons == 1 && posOk,
                  "dump: 256 chunk records; 435 eight-digit floats per pos line for the 255 Built chunks; the all-hole chunk has an empty idx; the Unsupported one a reason");
            Check(text.Contains("\nchunk 3 3 0 Built 000000000000c0c0 4 240\n") && text.Contains("\nchunk 6 6 0 Unsupported 0000000000000000 0 0\nreason hole mask"),
                  "dump: chunk records carry state, hole mask, hole cells and triangles");
            int k0 = text.IndexOf("\npos ", StringComparison.Ordinal);
            Check(text.Substring(k0 + 5, 26) == "80000000 42480000 00000000",
                  "dump: floats as bit patterns; the north-west vertex is (-0.0, 50, 0): ux = -(Y - Y0) gives -0.0 on the first column (" + text.Substring(k0 + 5, 26) + ")");
            int idxAt = text.IndexOf("\nidx ", StringComparison.Ordinal);
            Check(text.Substring(idxAt + 5, 12) == "9 0 1 9 1 18", "dump: indices in decimal, Unity order (" + text.Substring(idxAt + 5, 12) + ")");

            // Player extras.
            d.Root = new TerrainDumpUnity { Layer = 30, ActiveInHierarchy = true, LocalToWorld = new float[] { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } };
            d.Chunks[0].Unity = new TerrainDumpUnity
            {
                Layer = 30, ActiveInHierarchy = true, SubMeshCount = 1, Topology = "Triangles", IndexFormat = "UInt16",
                MeshBoundsMin = (float[])t.Chunks[0].BoundsMin.Clone(), MeshBoundsMax = (float[])t.Chunks[0].BoundsMax.Clone(), LocalToWorld = d.Root.LocalToWorld,
            };
            string px = TerrainDump.ToText(d);
            string rootLine = Array.Find(px.Split('\n'), l => l.StartsWith("root ", StringComparison.Ordinal));
            string unityLine = Array.Find(px.Split('\n'), l => l.StartsWith("unity ", StringComparison.Ordinal));
            Check(rootLine != null && rootLine.Split(' ').Length == 19 && rootLine.StartsWith("root 30 1 3f800000 00000000", StringComparison.Ordinal),
                  "dump: the player's root record (layer, active, 16 matrix floats)");
            Check(unityLine != null && unityLine.Split(' ').Length == 28 && unityLine.StartsWith("unity 30 1 1 Triangles UInt16 ", StringComparison.Ordinal),
                  "dump: the player's per-chunk unity record (layer, active, sub-meshes, topology, index format, bounds, matrix)");
            d.Chunks[0].Unity.LocalToWorld = new float[15];
            Throws<ArgumentException>(() => TerrainDump.ToText(d), "dump: a malformed matrix is refused, not written short");
        }

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();
            log.Add("Coordinates, conversion and identity tolerance");
            CoordinateTests();
            log.Add("Cell table");
            CellTableTests();
            log.Add("Flat tile");
            FlatTileTests();
            log.Add("Holes and bounds");
            HoleTests();
            log.Add("Ramps, orientation and the winding trap");
            RampTests();
            log.Add("Normals");
            NormalTests();
            log.Add("Failures and input rejection");
            FailureTests();
            log.Add("Thread safety");
            ThreadTests();
            log.Add("Mesh dump");
            DumpTests();
            log.Add(string.Format("TerrainMeshTests: {0} failure(s)", failures));
            if (output != null)
                foreach (string line in log)
                    output(line);
            return failures;
        }
    }
}
