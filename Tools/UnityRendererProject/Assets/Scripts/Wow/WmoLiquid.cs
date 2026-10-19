// WmoLiquid.cs
//
// MLIQ, the liquid surface of a WMO group, and its mesh in the group's own frame. Pure C#, no UnityEngine: the
// player builds the Unity object from the arrays; Tests/LiquidTests.cs and LiquidCorpusTests.cs check them.
//
// Evidence: docs/world-viewer/09-liquids.md 1.3 (every LOD0 group of every WMO root the listfile names, four
// clients, 2,801 MLIQ chunks; PROVEN_BYTES unless noted):
//   - MLIQ is a MOGP sub-chunk; MOGP flag 0x1000 <=> exactly one MLIQ, in every group (the root's MOGI cannot
//     tell). LOD1-3 group files carry no liquid.
//   - Header, 30 bytes: +0 i32 X (vertices along the group's local x), +4 i32 Y, +8 i32 A (tiles along x, X - 1),
//     +12 i32 B (Y - 1), +16 f32 x 3 corner (group frame), +28 u16 material index into the root's MOMT (0xFFFF in
//     two files; whether the game draws with it is UNKNOWN, it is kept raw). Then X * Y 8-byte vertices, then A * B
//     tile bytes: size == 30 + 8 X Y + A B in 2,801 of 2,801 -- enforced here, with X == A + 1 and Y == B + 1.
//   - Grid: vertex (i, j) = corner + (i S, j S, height) in the group's frame (the MOVT frame), S = 100/24 yd; x
//     varies fastest: vertex j X + i, tile j A + i; tile (i, j) uses vertices (i, j), (i + 1, j), (i, j + 1),
//     (i + 1, j + 1). (98% of depth bytes match the group's own floor below under this reading; transposed 34%.)
//   - Vertex: +4 f32 height; +0..3 for water and ocean a depth byte then three zero bytes, for magma and slime two
//     i16 (s, t) smooth over the grid (that they are texture coordinates is INFERRED, their scale UNKNOWN). Nothing
//     in MLIQ says which: the liquid type does, so both readings of the four bytes are kept (VertexWords).
//   - Tile byte: low nibble 0xF = no liquid in this tile (absent tiles' vertices are zero); otherwise one nibble
//     value per MLIQ, which in old files (root MOHD flag 0x4 clear) carries the liquid's identity; bits 0x40 and
//     0x80 vary, meaning UNKNOWN.
//   - The liquid lies outside the MOGP box in 22 groups: bounds come from the liquid's own vertices.
// A malformed MLIQ is reported (problem text) and gives no liquid; it never fails the group.

using System;
using Wmv.Wow.World;

namespace Wmv.Wow
{
    public sealed class WmoLiquid
    {
        public const int HeaderSize = 30;
        public const int VertexSize = 8;
        /// <summary>The grid step S = 100/24 yd (a terrain cell), in the group's frame.</summary>
        public const double TileSize = 100.0 / 24.0;
        /// <summary>Root MOHD flag: MOGP +0x34 is a LiquidType id (set), or a constant whose liquid comes from the
        /// tile nibble (clear).</summary>
        public const ushort RootFlagLiquidTypeInGroup = 0x4;
        /// <summary>MOGP flag the documented type rule reads for water (its meaning UNKNOWN; 24 Retail groups).</summary>
        public const uint GroupFlag80000 = 0x80000;

        public int VerticesX, VerticesY;          // X, Y
        public int TilesX, TilesY;                // A = X - 1, B = Y - 1
        public WowVec3 Corner;                    // group frame
        public ushort Material;                   // index into the root's MOMT, as stored
        public float[] Heights;                   // X * Y, vertex (i, j) at j X + i
        /// <summary>The first four bytes of each vertex as a little-endian u32: a depth byte (low byte) for water and
        /// ocean, two i16 (s low, t high) for magma and slime.</summary>
        public uint[] VertexWords;
        public byte[] Tiles;                      // A * B, tile (i, j) at j A + i
        public int PresentTiles;
        /// <summary>The low nibble of the present tiles (one value per MLIQ in every client file), -1 without one.</summary>
        public int Nibble = -1;
        public bool MixedNibbles;                 // present tiles carry more than one nibble (never in a client file)
        public int DataOffset;                    // payload offset in the bytes it was parsed from

        public int VertexIndex(int i, int j)
        {
            if (i < 0 || i >= VerticesX || j < 0 || j >= VerticesY)
                throw new ArgumentOutOfRangeException("i/j", "MLIQ vertex outside the grid");
            return j * VerticesX + i;
        }

        /// <summary>Whether tile (i, j) holds liquid: its low nibble is not 0xF (false outside the grid).</summary>
        public bool Present(int i, int j)
        {
            if (i < 0 || i >= TilesX || j < 0 || j >= TilesY)
                return false;
            return (Tiles[j * TilesX + i] & 0x0F) != 0x0F;
        }

        public float Height(int i, int j) { return Heights[VertexIndex(i, j)]; }

        /// <summary>The water/ocean reading of vertex (i, j): its depth byte.</summary>
        public byte Depth(int i, int j) { return (byte)VertexWords[VertexIndex(i, j)]; }

        /// <summary>The magma/slime reading of vertex (i, j): the i16 at +0.</summary>
        public short S(int i, int j) { return unchecked((short)VertexWords[VertexIndex(i, j)]); }

        /// <summary>The magma/slime reading of vertex (i, j): the i16 at +2.</summary>
        public short T(int i, int j) { return unchecked((short)(VertexWords[VertexIndex(i, j)] >> 16)); }

        /// <summary>Whether a present tile uses vertex (i, j).</summary>
        public bool VertexUsed(int i, int j)
        {
            return Present(i - 1, j - 1) || Present(i, j - 1) || Present(i - 1, j) || Present(i, j);
        }

        /// <summary>The group's MLIQ, or null: null with a null problem when the group has none, null with the
        /// reason when it is malformed (or MOGP flag 0x1000 and MLIQ disagree).</summary>
        public static WmoLiquid FromGroup(WmoGroup group, out string problem)
        {
            if (group == null)
                throw new ArgumentNullException("group");
            problem = null;
            int count = 0;
            WmoChunkInfo found = default(WmoChunkInfo);
            foreach (WmoChunkInfo c in group.Chunks)
                if (c.Tag == "MLIQ")
                {
                    if (count == 0)
                        found = c;
                    count++;
                }
            bool flag = (group.Header.Flags & WmoGroupFlags.Liquid) != 0;
            if (count == 0)
            {
                if (flag)
                    problem = "MOGP flag 0x1000 is set but the group has no MLIQ (they agree in every client file)";
                return null;
            }
            if (count > 1)
            {
                problem = string.Format("{0} MLIQ chunks in one group (exactly one in every client file)", count);
                return null;
            }
            if (!flag)
            {
                problem = "the group has an MLIQ but MOGP flag 0x1000 is clear (they agree in every client file)";
                return null;
            }
            return Parse(group.Data, found.DataOffset, found.Size, out problem);
        }

        /// <summary>Parses an MLIQ payload of `size` bytes at `offset` of `data`. Returns null with the reason when it
        /// is malformed: outside the buffer, shorter than its header, counts other than X = A + 1, Y = B + 1 with A,
        /// B >= 0, a size other than 30 + 8 X Y + A B, a non-finite corner, or an unusable height at a vertex a
        /// present tile uses.</summary>
        public static WmoLiquid Parse(byte[] data, int offset, int size, out string problem)
        {
            problem = null;
            if (data == null || offset < 0 || size < 0 || offset > data.Length || size > data.Length - offset)
            {
                problem = "MLIQ extends past the end of the buffer";
                return null;
            }
            if (size < HeaderSize)
            {
                problem = string.Format("MLIQ holds {0} byte(s), less than its 30-byte header", size);
                return null;
            }
            int x = I32(data, offset), y = I32(data, offset + 4), a = I32(data, offset + 8), b = I32(data, offset + 12);
            if (a < 0 || b < 0 || (long)x != (long)a + 1 || (long)y != (long)b + 1)
            {
                problem = string.Format("MLIQ counts X {0} Y {1} A {2} B {3} are not X = A + 1, Y = B + 1 (as in every client file)", x, y, a, b);
                return null;
            }
            // The vertices alone must fit before the equation is computed: 1 <= X, Y < 2^31, so X Y is exact in 64 bits, while
            // 8 X Y could wrap and pass the equation by accident (and X Y overflow the arrays below). Then 8 X Y <= size and
            // A B < X Y, so the equation is exact.
            if ((long)x * y > (size - HeaderSize) / VertexSize)
            {
                problem = string.Format("MLIQ holds {0} byte(s), not 30 + 8 X Y + A B: its X {1} x Y {2} vertices alone need more", size, x, y);
                return null;
            }
            long want = HeaderSize + (long)VertexSize * x * y + (long)a * b;
            if (want != size)
            {
                problem = string.Format("MLIQ holds {0} byte(s), not 30 + 8 X Y + A B = {1} (X {2}, Y {3}, A {4}, B {5})", size, want, x, y, a, b);
                return null;
            }
            var liq = new WmoLiquid
            {
                VerticesX = x, VerticesY = y, TilesX = a, TilesY = b, DataOffset = offset,
                Corner = new WowVec3(F32(data, offset + 16), F32(data, offset + 20), F32(data, offset + 24)),
                Material = (ushort)(data[offset + 28] | (data[offset + 29] << 8)),
            };
            if (!Finite(liq.Corner.X) || !Finite(liq.Corner.Y) || !Finite(liq.Corner.Z))
            {
                problem = "MLIQ corner is not finite " + liq.Corner;
                return null;
            }
            int n = x * y;
            liq.Heights = new float[n];
            liq.VertexWords = new uint[n];
            int p = offset + HeaderSize;
            for (int k = 0; k < n; k++, p += VertexSize)
            {
                liq.VertexWords[k] = (uint)I32(data, p);
                liq.Heights[k] = F32(data, p + 4);
            }
            liq.Tiles = new byte[a * b];
            Buffer.BlockCopy(data, p, liq.Tiles, 0, liq.Tiles.Length);
            foreach (byte t in liq.Tiles)
            {
                if ((t & 0x0F) == 0x0F)
                    continue;
                liq.PresentTiles++;
                if (liq.Nibble < 0)
                    liq.Nibble = t & 0x0F;
                else if (liq.Nibble != (t & 0x0F))
                    liq.MixedNibbles = true;
            }
            for (int j = 0; j < y; j++)
                for (int i = 0; i < x; i++)
                {
                    if (!liq.VertexUsed(i, j))
                        continue;
                    float h = liq.Heights[j * x + i];
                    if (!Finite(h) || Math.Abs(h) > AdtParser.LiquidMaxAbsHeight)
                    {
                        problem = string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                                "MLIQ vertex ({0}, {1}) of a present tile has height {2:R}", i, j, h);
                        return null;
                    }
                }
            return liq;
        }

        /// <summary>
        /// The LiquidType id of a group's liquid by public documentation's rule (INFERRED; the kind it gives agrees
        /// with the bytes in every case they can test, 09-liquids.md 1.3). MOHD flag 0x4 set: MOGP +0x34 is the id,
        /// the basic ids below 21 mapped to the "WMO" rows by kind ((id - 1) &amp; 3). Clear: +0x34 is the constant 15
        /// and the first present tile's nibble gives the kind (nibble &amp; 3); other values below 20 give the kind
        /// themselves, larger ones are id - 1. The "WMO" rows: water 13 (14 with MOGP flag 0x80000), ocean 14, magma
        /// 19, slime 20. Returns 0 when nothing names a type: a 0 at +0x34 with flag 0x4 set (where the documented
        /// expression would wrap to slime), or the constant 15 without a present tile.
        /// </summary>
        public static int GroupLiquidType(ushort rootFlags, uint groupLiquid, uint groupFlags, int firstPresentNibble)
        {
            if (groupLiquid >= int.MaxValue)
                return 0;
            if ((rootFlags & RootFlagLiquidTypeInGroup) != 0)
            {
                if (groupLiquid == 0)
                    return 0;
                return groupLiquid < 21 ? WmoBasic((int)((groupLiquid - 1) & 3), groupFlags) : (int)groupLiquid;
            }
            if (groupLiquid == 15)
                return firstPresentNibble < 0 ? 0 : WmoBasic(firstPresentNibble & 3, groupFlags);
            return groupLiquid < 20 ? WmoBasic((int)(groupLiquid & 3), groupFlags) : (int)groupLiquid + 1;
        }

        static int WmoBasic(int kind, uint groupFlags)
        {
            switch (kind)
            {
                case 0: return (groupFlags & GroupFlag80000) != 0 ? 14 : 13;
                case 1: return 14;
                case 2: return 19;
                default: return 20;
            }
        }

        static int I32(byte[] d, int o)
        {
            return d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24);
        }

        static float F32(byte[] d, int o) { return BitConverter.Int32BitsToSingle(I32(d, o)); }

        static bool Finite(float f) { return !float.IsNaN(f) && !float.IsInfinity(f); }
    }

    /// <summary>
    /// A group's MLIQ as one LiquidSurface in the group's own frame, converted like MOVT (WowCoordinateConverter), so
    /// the world model's placement carries it like the group's geometry; one mesh, shared by every placement.
    /// Only present tiles make quads; vertices are shared within the MLIQ and only used ones are emitted (scanning
    /// tiles x fastest). Tile (i, j) with p00 = (i, j), p10 = (i + 1, j), p01 = (i, j + 1), p11 = (i + 1, j + 1)
    /// makes (p00, p11, p10) and (p00, p01, p11): front faces up in Unity (the diagonal is a choice, as for MH2O).
    ///
    /// The vertex payload follows the liquid type's LVF from the catalogue: an LVF whose MH2O format stores u/v
    /// (1 or 3) reads the four bytes as (s, t), an LVF of 0 or 2 as a depth byte. (The Stage 09 MLIQ census
    /// partitions the bytes the same way, PROVEN_BYTES: the types of LiquidMaterial 2 (LVF 1) and 13, 14, 18 (LVF 3)
    /// carry the i16 pattern, those of 1, 5, 10, 12, 130 (LVF 0) the depth pattern; e.g. Retail's "10FX" lava, LVF 3.)
    /// A type without an LVF falls back to its kind (magma and slime: s, t); without either, depth.
    ///   uv0: (s, t) as stored (i16, not unwrapped: neighbouring steps stay far from the wrap) times
    ///        LiquidFormat.StoredUvScale, or for the depth reading planar in the group frame as for MH2O
    ///        (TerrainLiquidMesh.PlanarUv on the group-frame x, y, the type's Float0 and Float1); shifted by the floor
    ///        of the first used vertex's value;
    ///   uv1: (depth / 255, 1) for the depth reading, (1, 0) for s, t.
    /// </summary>
    public static class WmoLiquidMesh
    {
        /// <summary>Whether the type's vertices carry (s, t) rather than a depth byte (see the class summary).</summary>
        public static bool CarriesSt(LiquidTypeView type)
        {
            if (type == null)
                return false;
            if (type.Lvf >= 0 && type.Lvf <= 3)
                return LiquidFormat.HasUv(type.Lvf);
            return type.Kind == (int)LiquidKind.Magma || type.Kind == (int)LiquidKind.Slime;
        }

        /// <summary>The MLIQ's surface for LiquidType `liquidType` (WmoLiquid.GroupLiquidType); `catalog` may be null.
        /// A liquid without present tiles gives a surface without vertices.</summary>
        public static LiquidSurface Build(WmoLiquid liquid, int liquidType, ILiquidCatalog catalog)
        {
            if (liquid == null)
                throw new ArgumentNullException("liquid");
            LiquidTypeView type;
            if (catalog == null || !catalog.TryGetType(liquidType, out type))
                type = null;
            bool st = CarriesSt(type);
            double repeats = TerrainLiquidMesh.DefaultRepeatsPerChunk, degrees = 0;
            if (type != null)
            {
                if (!float.IsNaN(type.Float0) && !float.IsInfinity(type.Float0)) repeats = type.Float0;
                if (!float.IsNaN(type.Float1) && !float.IsInfinity(type.Float1)) degrees = type.Float1;
            }
            var b = new LiquidSurfaceBuilder(liquidType, type);
            var index = new int[liquid.VerticesX * liquid.VerticesY];
            for (int k = 0; k < index.Length; k++)
                index[k] = -1;
            bool haveShift = false;
            double shiftU = 0, shiftV = 0;
            for (int j = 0; j < liquid.TilesY; j++)
                for (int i = 0; i < liquid.TilesX; i++)
                {
                    if (!liquid.Present(i, j))
                        continue;
                    if (!haveShift)
                    {
                        double u, v;
                        RawUv(liquid, i, j, st, repeats, degrees, out u, out v);
                        shiftU = Math.Floor(u);
                        shiftV = Math.Floor(v);
                        haveShift = true;
                    }
                    int p00 = Vertex(b, liquid, index, i, j, st, repeats, degrees, shiftU, shiftV);
                    int p10 = Vertex(b, liquid, index, i + 1, j, st, repeats, degrees, shiftU, shiftV);
                    int p01 = Vertex(b, liquid, index, i, j + 1, st, repeats, degrees, shiftU, shiftV);
                    int p11 = Vertex(b, liquid, index, i + 1, j + 1, st, repeats, degrees, shiftU, shiftV);
                    b.AddTriangle(p00, p11, p10);
                    b.AddTriangle(p00, p01, p11);
                }
            LiquidSurface s = b.Finish();
            if (s.QuadCount > 0)
            {
                s.Instances = 1;
                if (st) s.StoredUvInstances = 1;
                else s.PlanarUvInstances = 1;
            }
            return s;
        }

        /// <summary>Vertex (i, j) in the group's frame (WoW axes, before conversion), in double.</summary>
        static void GroupPosition(WmoLiquid liquid, int i, int j, out double x, out double y)
        {
            x = liquid.Corner.X + i * WmoLiquid.TileSize;
            y = liquid.Corner.Y + j * WmoLiquid.TileSize;
        }

        static void RawUv(WmoLiquid liquid, int i, int j, bool st, double repeats, double degrees, out double u, out double v)
        {
            if (st)
            {
                u = liquid.S(i, j) * (double)LiquidFormat.StoredUvScale;
                v = liquid.T(i, j) * (double)LiquidFormat.StoredUvScale;
                return;
            }
            double x, y;
            GroupPosition(liquid, i, j, out x, out y);
            TerrainLiquidMesh.PlanarUv(x, y, repeats, degrees, out u, out v);
        }

        static int Vertex(LiquidSurfaceBuilder b, WmoLiquid liquid, int[] index, int i, int j, bool st, double repeats, double degrees,
                          double shiftU, double shiftV)
        {
            int k = liquid.VertexIndex(i, j);
            if (index[k] >= 0)
                return index[k];
            double x, y;
            GroupPosition(liquid, i, j, out x, out y);
            float ux, uy, uz;
            WowCoordinateConverter.ConvertPosition(new WowVec3((float)x, (float)y, liquid.Heights[k]), out ux, out uy, out uz);
            double u, v;
            RawUv(liquid, i, j, st, repeats, degrees, out u, out v);
            float d1 = 1f, d2 = 0f;
            if (!st)
            {
                d1 = liquid.Depth(i, j) / 255f;
                d2 = 1f;
            }
            index[k] = b.AddVertex(ux, uy, uz, (float)(u - shiftU), (float)(v - shiftV), d1, d2);
            return index[k];
        }
    }
}
