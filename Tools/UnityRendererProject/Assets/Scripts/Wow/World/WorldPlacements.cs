// WorldPlacements.cs
//
// Placement records as stored (MDDF in obj0, MLDD in obj1: doodads; MODF in obj0 and in a WMO-only
// WDT: map objects), and the pure, separately tested transform the audit proved for them.
//
// Field evidence (docs/world-viewer/00-audit.md):
//   MDDF / MLDD, 36 bytes (5.6): u32 NameId @0 (a FileDataID on every record of the four clients;
//     every record carries flag 0x40, so 0x40 = "FileDataID" is a 100% correlation, not an
//     isolated meaning), u32 UniqueId @4, f32 x3 Position @8 (file frame, 6.3), f32 x3 Rotation
//     @20 (degrees, 6.4), u16 Scale @32 (u16/1024, proven by the obj1 MLDX boxes, 6.4), u16 Flags
//     @34 (only 0x40 correlated). MLDD holds the same placements as obj0 MDDF, matched by uniqueId
//     (same name, position, rotation, scale; flags gain 0x20/0x08/0x10) -- NOT by index: the order
//     differs on Retail 29_48 and 30_48. MLDX[i] belongs to MLDD[i], never to MDDF[i].
//   MODF, 64 bytes (4.3, 5.6, 6.4): u32 NameId @0 (a FileDataID when flag 0x8 -- every ADT MODF
//     and the modern WDT form; the old WDT form has flag 0 and names an MWMO entry), u32 UniqueId
//     @4, f32 x3 Position @8, f32 x3 Rotation @20, f32 x3 ExtentsMin @32 and f32 x3 ExtentsMax @44
//     (file frame; they are the exact world box: 201 of 202 Retail placements to 0.1 yd, 6.4),
//     u16 Flags @56, u16 @58 and u16 @60 (kept raw: the Python reader calls them doodad set /
//     name set, not verified, U15), u16 Scale @62 (1024 on every ADT MODF; 0 on the old WDT form).
//   MLDX, 28 bytes per MLDD record (5.6, 6.4): f32 x6 = the placed doodad's world box (min, max,
//     world frame), then a 7th f32 kept raw (meaning not established).
//
// Transform (6.3, 6.4), all in double, in the GAME frame (no Unity conversion here):
//   world position W = (32T - p[2], 32T - p[0], p[1])
//   R = Rz(a1 + 180) * Ry(a0) * Rx(a2)   (degrees, world axes; Rx acts first)
//   world point = W + scale * R * model point
// Measured fit: MODF 201/202 within 0.1 yd (the misfit, uniqueId 2848560 / WMO 112871, is a root
// box that disagrees with its own group box); MLDX 25,621/25,648 within 0.5 yd. Not every record
// fits; nothing here hides that.
//
// THE WDT'S GLOBAL MODF (stage 06, docs/world-viewer/06-world-models-in-world.md section 2.3) is the same
// record in a frame WITHOUT the map-centre offset: W = (-p[2], -p[0], p[1]), the same rotation and scale.
// Evidence, all four clients: the client's own AreaTrigger positions on map 34 and map 429 lie inside the
// global WMO's group boxes under exactly that rule (8 of 8 on Dire Maul, with the yaw: Era's file stores
// the model turned 180 degrees and its record carries a1 = 180; the other clients store a1 = 0), and the
// record's extents equal the placed root box under it; with the map-centre offset every such position
// would lie some 17,000 yards away. Every modern-form record is at position (0, 0, 0): a non-zero
// position in this frame is INFERRED, not observed.

using System;

namespace Wmv.Wow.World
{
    /// <summary>A position or vector in the game's world frame (x, y horizontal, z up), double.</summary>
    public struct WorldPoint
    {
        public double X, Y, Z;
        public WorldPoint(double x, double y, double z) { X = x; Y = y; Z = z; }
        public override string ToString() { return string.Format("({0:F4}, {1:F4}, {2:F4})", X, Y, Z); }
    }

    /// <summary>Three floats exactly as stored (file frame or degrees, see the record).</summary>
    public struct WorldFloat3
    {
        public float A, B, C;
        public WorldFloat3(float a, float b, float c) { A = a; B = b; C = c; }
        public override string ToString() { return string.Format("({0}, {1}, {2})", A, B, C); }
    }

    /// <summary>What a placement's NameId refers to, as far as the audited data establishes.</summary>
    public enum PlacementNameKind
    {
        /// <summary>A FileDataID (MDDF flag 0x40 / MODF flag 0x8 -- true of every record in the
        /// audited clients).</summary>
        FileDataId,
        /// <summary>Not a FileDataID: an index or byte offset into a name table (MWMO in an old
        /// WMO-only WDT); which of the two cannot be told from the audited data.</summary>
        NameTableEntry,
    }

    /// <summary>Where a placement record was read: lets a later scene builder decide which records
    /// with the same uniqueId are the same world instance, instead of the parser discarding any.</summary>
    public struct PlacementSource
    {
        public int TileX, TileY;       // -1 for a WDT record
        public string Container;       // "obj0 MDDF", "obj1 MLDD", "obj0 MODF", "WDT MODF"
        public int RecordIndex;        // index within that chunk
    }

    /// <summary>MDDF / MLDD record (36 bytes).</summary>
    public struct DoodadPlacement
    {
        public uint NameId;
        public uint UniqueId;
        public WorldFloat3 Position;   // file frame
        public WorldFloat3 Rotation;   // degrees: a0, a1, a2 as stored
        public ushort ScaleRaw;        // scale = ScaleRaw / 1024
        public ushort Flags;
        public PlacementSource Source;

        public PlacementNameKind NameKind
        {
            get { return (Flags & 0x40) != 0 ? PlacementNameKind.FileDataId : PlacementNameKind.NameTableEntry; }
        }

        public double Scale { get { return ScaleRaw / 1024.0; } }

        /// <summary>True when name, position, rotation and scale are identical (the audit's test for
        /// "the same placement repeated in another tile"); flags are compared separately because
        /// MLDD copies gain bits.</summary>
        public bool SamePlacementAs(DoodadPlacement o)
        {
            return NameId == o.NameId && UniqueId == o.UniqueId && SameBits(Position, o.Position) &&
                   SameBits(Rotation, o.Rotation) && ScaleRaw == o.ScaleRaw;
        }

        internal static bool SameBits(WorldFloat3 a, WorldFloat3 b)
        {
            return BitConverter.SingleToInt32Bits(a.A) == BitConverter.SingleToInt32Bits(b.A) &&
                   BitConverter.SingleToInt32Bits(a.B) == BitConverter.SingleToInt32Bits(b.B) &&
                   BitConverter.SingleToInt32Bits(a.C) == BitConverter.SingleToInt32Bits(b.C);
        }
    }

    /// <summary>MODF record (64 bytes), in an obj0 file or a WMO-only WDT.</summary>
    public struct WmoPlacement
    {
        public uint NameId;
        public uint UniqueId;
        public WorldFloat3 Position;     // file frame
        public WorldFloat3 Rotation;     // degrees
        public WorldFloat3 ExtentsMin;   // file frame, exact world box (6.4)
        public WorldFloat3 ExtentsMax;
        public ushort Flags;
        public ushort Word58;            // raw (called doodad set by the Python reader; not verified, U15)
        public ushort Word60;            // raw (called name set by the Python reader; not verified)
        public ushort ScaleRaw;          // 1024 on every ADT MODF; 0 on the old WDT form
        public PlacementSource Source;

        public PlacementNameKind NameKind
        {
            get { return (Flags & 0x8) != 0 ? PlacementNameKind.FileDataId : PlacementNameKind.NameTableEntry; }
        }

        /// <summary>ScaleRaw / 1024 when flag 0x4 is set (every ADT MODF: always 1024, so 1.0), else
        /// 1.0 -- the old WDT form stores 0 without the flag; 1.0 there is INFERRED (audit 4.3).</summary>
        public double Scale { get { return (Flags & 0x4) != 0 ? ScaleRaw / 1024.0 : 1.0; } }

        public bool SamePlacementAs(WmoPlacement o)
        {
            return NameId == o.NameId && UniqueId == o.UniqueId && DoodadPlacement.SameBits(Position, o.Position) &&
                   DoodadPlacement.SameBits(Rotation, o.Rotation) && DoodadPlacement.SameBits(ExtentsMin, o.ExtentsMin) &&
                   DoodadPlacement.SameBits(ExtentsMax, o.ExtentsMax) && Flags == o.Flags && Word58 == o.Word58 &&
                   Word60 == o.Word60 && ScaleRaw == o.ScaleRaw;
        }
    }

    /// <summary>MLDX record (28 bytes): the placed doodad's world box and a 7th value kept raw.</summary>
    public struct DoodadWorldBox
    {
        public WorldFloat3 Min;          // world frame (X, Y, Z)
        public WorldFloat3 Max;
        public float Value6Raw;          // meaning not established
    }

    public static class PlacementRecords
    {
        public const int DoodadRecordSize = 36;
        public const int WmoRecordSize = 64;
        public const int DoodadBoxRecordSize = 28;

        public static DoodadPlacement[] ReadDoodads(WorldSpan s, int tileX, int tileY, string container)
        {
            int n = s.RecordCount(DoodadRecordSize, container);
            var r = new DoodadPlacement[n];
            for (int i = 0; i < n; i++)
            {
                int o = DoodadRecordSize * i;
                r[i] = new DoodadPlacement
                {
                    NameId = s.U32(o),
                    UniqueId = s.U32(o + 4),
                    Position = new WorldFloat3(s.F32(o + 8), s.F32(o + 12), s.F32(o + 16)),
                    Rotation = new WorldFloat3(s.F32(o + 20), s.F32(o + 24), s.F32(o + 28)),
                    ScaleRaw = s.U16(o + 32),
                    Flags = s.U16(o + 34),
                    Source = new PlacementSource { TileX = tileX, TileY = tileY, Container = container, RecordIndex = i },
                };
            }
            return r;
        }

        public static WmoPlacement[] ReadWmos(WorldSpan s, int tileX, int tileY, string container)
        {
            int n = s.RecordCount(WmoRecordSize, container);
            var r = new WmoPlacement[n];
            for (int i = 0; i < n; i++)
            {
                int o = WmoRecordSize * i;
                r[i] = new WmoPlacement
                {
                    NameId = s.U32(o),
                    UniqueId = s.U32(o + 4),
                    Position = new WorldFloat3(s.F32(o + 8), s.F32(o + 12), s.F32(o + 16)),
                    Rotation = new WorldFloat3(s.F32(o + 20), s.F32(o + 24), s.F32(o + 28)),
                    ExtentsMin = new WorldFloat3(s.F32(o + 32), s.F32(o + 36), s.F32(o + 40)),
                    ExtentsMax = new WorldFloat3(s.F32(o + 44), s.F32(o + 48), s.F32(o + 52)),
                    Flags = s.U16(o + 56),
                    Word58 = s.U16(o + 58),
                    Word60 = s.U16(o + 60),
                    ScaleRaw = s.U16(o + 62),
                    Source = new PlacementSource { TileX = tileX, TileY = tileY, Container = container, RecordIndex = i },
                };
            }
            return r;
        }

        public static DoodadWorldBox[] ReadDoodadBoxes(WorldSpan s)
        {
            int n = s.RecordCount(DoodadBoxRecordSize, "MLDX");
            var r = new DoodadWorldBox[n];
            for (int i = 0; i < n; i++)
            {
                int o = DoodadBoxRecordSize * i;
                r[i] = new DoodadWorldBox
                {
                    Min = new WorldFloat3(s.F32(o), s.F32(o + 4), s.F32(o + 8)),
                    Max = new WorldFloat3(s.F32(o + 12), s.F32(o + 16), s.F32(o + 20)),
                    Value6Raw = s.F32Raw(o + 24),
                };
            }
            return r;
        }
    }

    /// <summary>The frame a MODF record's position and extents are stored in.</summary>
    public enum PlacementFrame
    {
        /// <summary>An ADT's obj0 MODF: relative to the map's north-west corner (the map-centre offset 32T applies).</summary>
        Tile,
        /// <summary>A WMO-only WDT's MODF: relative to the world origin (no offset; see the file header).</summary>
        Global,
    }

    /// <summary>The proven placement transform (audit 6.3, 6.4), double precision, game frame.</summary>
    public static class PlacementTransform
    {
        /// <summary>32 tiles: the map centre (corner of tile 32,32) in world units.</summary>
        public const double MapHalf = 32.0 * TerrainCoordinates.TileSize;

        /// <summary>The offset a frame's positions and extents are stored against: MapHalf for a tile's records, 0 for
        /// the WDT's global record.</summary>
        public static double Offset(PlacementFrame frame) { return frame == PlacementFrame.Tile ? MapHalf : 0.0; }

        /// <summary>File-frame position p -> world, in the frame the record was stored in.</summary>
        public static WorldPoint ToWorld(WorldFloat3 p, PlacementFrame frame)
        {
            double h = Offset(frame);
            return new WorldPoint(h - p.C, h - p.A, p.B);
        }

        /// <summary>world = W + scale * R * model, in the frame the record was stored in.</summary>
        public static WorldPoint Transform(WorldFloat3 position, WorldFloat3 rotation, double scale, WorldPoint model, PlacementFrame frame)
        {
            return Apply(Rotation(rotation), ToWorld(position, frame), scale, model);
        }

        /// <summary>The world box a model box makes once placed, in the frame the record was stored in.</summary>
        public static void PlacedBox(WorldFloat3 position, WorldFloat3 rotation, double scale, WorldPoint modelMin, WorldPoint modelMax,
                                     PlacementFrame frame, out WorldPoint min, out WorldPoint max)
        {
            double[] m = Rotation(rotation);
            WorldPoint w = ToWorld(position, frame);
            min = new WorldPoint(double.MaxValue, double.MaxValue, double.MaxValue);
            max = new WorldPoint(double.MinValue, double.MinValue, double.MinValue);
            for (int k = 0; k < 8; k++)
            {
                var corner = new WorldPoint((k & 1) == 0 ? modelMin.X : modelMax.X, (k & 2) == 0 ? modelMin.Y : modelMax.Y,
                                            (k & 4) == 0 ? modelMin.Z : modelMax.Z);
                WorldPoint q = Apply(m, w, scale, corner);
                min = new WorldPoint(Math.Min(min.X, q.X), Math.Min(min.Y, q.Y), Math.Min(min.Z, q.Z));
                max = new WorldPoint(Math.Max(max.X, q.X), Math.Max(max.Y, q.Y), Math.Max(max.Z, q.Z));
            }
        }

        /// <summary>MODF extents (file frame) as the world box they are, in the frame the record was stored in.</summary>
        public static void ExtentsToWorld(WorldFloat3 extentsMin, WorldFloat3 extentsMax, PlacementFrame frame, out WorldPoint min, out WorldPoint max)
        {
            double h = Offset(frame);
            min = new WorldPoint(h - extentsMax.C, h - extentsMax.A, extentsMin.B);
            max = new WorldPoint(h - extentsMin.C, h - extentsMin.A, extentsMax.B);
        }

        /// <summary>
        /// A world rotation R (row-major, game frame) as the rotation of a mesh already in Unity's axes: U = M * R * M^T with
        /// M the model-space map unity = (-y, z, x) (WowCoordinateConverter, WorldToUnity). M is orthogonal with det -1, so
        /// U is a proper rotation (det +1) and a converted mesh turned by U keeps its winding. Row-major 3x3.
        /// </summary>
        public static double[] UnityRotation(double[] r)
        {
            // M rows: (0,-1,0), (0,0,1), (1,0,0). (M R M^T)[i][j] = sum_k,l M[i][k] R[k][l] M[j][l].
            var m = new[] { 0.0, -1.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0 };
            var u = new double[9];
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                {
                    double s = 0.0;
                    for (int k = 0; k < 3; k++)
                        for (int l = 0; l < 3; l++)
                            s += m[3 * i + k] * r[3 * k + l] * m[3 * j + l];
                    u[3 * i + j] = s;
                }
            return u;
        }

        /// <summary>The determinant of a row-major 3x3.</summary>
        public static double Determinant(double[] a)
        {
            return a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) + a[2] * (a[3] * a[7] - a[4] * a[6]);
        }

        /// <summary>File-frame position p -> world (32T - p[2], 32T - p[0], p[1]).</summary>
        public static WorldPoint ToWorld(WorldFloat3 p)
        {
            return new WorldPoint(MapHalf - p.C, MapHalf - p.A, p.B);
        }

        /// <summary>Row-major 3x3: Rz(a1 + 180) * Ry(a0) * Rx(a2), angles in degrees as stored.</summary>
        public static double[] Rotation(WorldFloat3 rotation)
        {
            double[] m = Mul(Mul(Rot(2, rotation.B + 180.0), Rot(1, rotation.A)), Rot(0, rotation.C));
            return m;
        }

        /// <summary>world = W + scale * R * model.</summary>
        public static WorldPoint Transform(WorldFloat3 position, WorldFloat3 rotation, double scale, WorldPoint model)
        {
            double[] m = Rotation(rotation);
            WorldPoint w = ToWorld(position);
            return Apply(m, w, scale, model);
        }

        /// <summary>The world-axis-aligned box of a model box placed by the transform (all 8 corners).</summary>
        public static void PlacedBox(WorldFloat3 position, WorldFloat3 rotation, double scale, WorldPoint modelMin,
                                     WorldPoint modelMax, out WorldPoint min, out WorldPoint max)
        {
            double[] m = Rotation(rotation);
            WorldPoint w = ToWorld(position);
            min = new WorldPoint(double.MaxValue, double.MaxValue, double.MaxValue);
            max = new WorldPoint(double.MinValue, double.MinValue, double.MinValue);
            for (int k = 0; k < 8; k++)
            {
                var corner = new WorldPoint((k & 1) == 0 ? modelMin.X : modelMax.X, (k & 2) == 0 ? modelMin.Y : modelMax.Y,
                                            (k & 4) == 0 ? modelMin.Z : modelMax.Z);
                WorldPoint q = Apply(m, w, scale, corner);
                min = new WorldPoint(Math.Min(min.X, q.X), Math.Min(min.Y, q.Y), Math.Min(min.Z, q.Z));
                max = new WorldPoint(Math.Max(max.X, q.X), Math.Max(max.Y, q.Y), Math.Max(max.Z, q.Z));
            }
        }

        /// <summary>MODF extents (file frame) as the world box they are: X from 32T - max[2] to
        /// 32T - min[2], Y from 32T - max[0] to 32T - min[0], Z from min[1] to max[1].</summary>
        public static void ExtentsToWorld(WorldFloat3 extentsMin, WorldFloat3 extentsMax, out WorldPoint min, out WorldPoint max)
        {
            min = new WorldPoint(MapHalf - extentsMax.C, MapHalf - extentsMax.A, extentsMin.B);
            max = new WorldPoint(MapHalf - extentsMin.C, MapHalf - extentsMin.A, extentsMax.B);
        }

        static WorldPoint Apply(double[] m, WorldPoint w, double s, WorldPoint v)
        {
            double x = s * v.X, y = s * v.Y, z = s * v.Z;
            double rx = m[0] * x + m[1] * y + m[2] * z;
            double ry = m[3] * x + m[4] * y + m[5] * z;
            double rz = m[6] * x + m[7] * y + m[8] * z;
            return new WorldPoint(w.X + rx, w.Y + ry, w.Z + rz);
        }

        /// <summary>Right-handed rotation about world axis 0 (X), 1 (Y) or 2 (Z), degrees.</summary>
        public static double[] Rot(int axis, double degrees)
        {
            double a = degrees * (Math.PI / 180.0);
            double c = Math.Cos(a), s = Math.Sin(a);
            if (axis == 0)
                return new[] { 1.0, 0.0, 0.0, 0.0, c, -s, 0.0, s, c };
            if (axis == 1)
                return new[] { c, 0.0, s, 0.0, 1.0, 0.0, -s, 0.0, c };
            return new[] { c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0 };
        }

        public static double[] Mul(double[] a, double[] b)
        {
            var r = new double[9];
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    r[3 * i + j] = a[3 * i] * b[j] + a[3 * i + 1] * b[3 + j] + a[3 * i + 2] * b[6 + j];
            return r;
        }
    }
}
