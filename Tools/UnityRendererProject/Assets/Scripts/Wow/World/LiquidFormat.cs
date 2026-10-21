// LiquidFormat.cs
//
// What the MH2O and MLIQ mesh builders (TerrainLiquidMesh.cs, Wow/WmoLiquid.cs) share: the vertex formats and
// their rule, the view of the host's liquid catalogue the player is given, and the plain mesh arrays both
// builders produce. Pure C#, no UnityEngine. The player reads no DB2 table: the host reads the liquid tables
// and sends, per LiquidType id, the columns this stage relies on (docs/world-viewer/09-liquids.md 1.6);
// ILiquidCatalog is the player's view of that catalogue.
//
// Evidence (docs/world-viewer/09-liquids.md 1.2 and 1.4):
//   - Vertex formats, by the bytes per vertex of an MH2O vertex block (PROVEN_BYTES, every instance of the
//     four clients' catalogued maps); n = (Width + 1)(Height + 1) vertices, each array in vertex order:
//         0: f32 height[n], u8 depth[n]                        5 bytes per vertex
//         1: f32 height[n], (u16 u, u16 v)[n]                  8
//         2: u8 depth[n]; the height is the instance's min     1
//         3: f32 height[n], (u16 u, u16 v)[n], u8 depth[n]     9
//   - The format rule, matching 6,334,582 of 6,334,582 instances (PROVEN_BYTES): the instance's second word
//     when it is 0, 1 or 2; otherwise 2 for liquid type 2 ("Ocean", which has depth-only data or none --
//     why that type is special is UNKNOWN); otherwise the LVF of the type's LiquidMaterial.
//   - Kind: LiquidType.SoundBank, a 2-bit class: 0 water, 1 ocean, 2 magma, 3 slime (PROVEN_BYTES as a class;
//     the MLIQ vertex payload follows it).
//   - The depth byte is a quantised depth -- the liquid vertex's height above the terrain's outer vertex under it --
//     on one of two scales, picked by the instance's second word, not by the liquid type (PROVEN_BYTES: a census of
//     15,384 Retail, 839 Era and 2,022 Beta roots, 99.5% of vertices and more): word 2 or 42, the O scale, byte =
//     clamp(ceil(12/7 D), 0, 255); any other word (0, or a LiquidObject id), the W scale, byte = clamp(floor(9 D), 0,
//     255). MLIQ's byte is the W scale. DepthYards decodes a byte to the middle of its step (or 0). What a stored
//     u/v measures, and on what scale, is UNKNOWN (09-liquids.md 1.2): passed through, scaled by StoredUvScale.

using System;
using System.Collections.Generic;

namespace Wmv.Wow.World
{
    /// <summary>LiquidType.SoundBank (PROVEN_BYTES as a 4-way class; the names are the base rows' own).</summary>
    public enum LiquidKind
    {
        Water = 0,
        Ocean = 1,
        Magma = 2,
        Slime = 3,
    }

    /// <summary>One row of the host's liquid catalogue (contract: the `liquids.catalog.types` entries).</summary>
    public sealed class LiquidTypeView
    {
        public int Id;                            // LiquidType id
        public string Name;                       // LiquidType.Name
        public int Kind = -1;                     // LiquidType.SoundBank (LiquidKind), -1 = not given
        public int Material;                      // LiquidType.MaterialID
        public int MaterialFlags;                 // LiquidMaterial.Flags (0x1 "transparent": INFERRED)
        public int Lvf = -1;                      // LiquidMaterial.LVF (vertex format 0..3), -1 = no material row
        public uint MinimapColor;                 // LiquidType.MinimapStaticCol, 0xAARRGGBB as stored (INFERRED colour)
        public float Float0;                      // LiquidType.Float[0]: texture repeats per chunk (INFERRED name)
        public float Float1;                      // LiquidType.Float[1]: texture rotation in degrees (INFERRED name)
        public uint[] Frames = new uint[0];       // texture slot 0's frames as FileDataIDs, in frame order
        public int FramesWanted;                  // frames slot 0 names (0 when empty or procedural)
        public uint Bump;                         // the ripple map: the first texture of slots 1-5 named "bump" (the host's
                                                  // rule, INFERRED), as a FileDataID; 0 = none (the High look's flat normals)

        public bool IsWaterOrOcean { get { return Kind == (int)LiquidKind.Water || Kind == (int)LiquidKind.Ocean; } }
    }

    /// <summary>The player's view of the host's liquid catalogue for the loaded client.</summary>
    public interface ILiquidCatalog
    {
        /// <summary>The row of a LiquidType id; false when the client's table has no such id (or the catalogue
        /// is unavailable).</summary>
        bool TryGetType(int liquidTypeId, out LiquidTypeView type);
    }

    /// <summary>A catalogue held in memory, keyed by LiquidType id.</summary>
    public sealed class LiquidCatalog : ILiquidCatalog
    {
        readonly Dictionary<int, LiquidTypeView> types = new Dictionary<int, LiquidTypeView>();

        public int Count { get { return types.Count; } }

        /// <summary>Adds or replaces the row of type.Id.</summary>
        public void Add(LiquidTypeView type)
        {
            if (type == null)
                throw new ArgumentNullException("type");
            types[type.Id] = type;
        }

        public bool TryGetType(int liquidTypeId, out LiquidTypeView type)
        {
            return types.TryGetValue(liquidTypeId, out type);
        }
    }

    /// <summary>Where a format decision came from (LiquidFormat.Decide).</summary>
    public enum LiquidFormatSource
    {
        /// <summary>No rule applied and the instance has no vertex data.</summary>
        None,
        /// <summary>The instance's second word is 0, 1 or 2: the format itself.</summary>
        Word,
        /// <summary>Liquid type 2 ("Ocean"): format 2.</summary>
        OceanType,
        /// <summary>The LVF of the type's LiquidMaterial, from the catalogue.</summary>
        Catalog,
        /// <summary>The catalogue lacks the type (or its material): the vertex block's size.</summary>
        Size,
    }

    public struct LiquidFormatDecision
    {
        public int Format;                        // 0..3, or -1
        public LiquidFormatSource Source;
        /// <summary>The rule and the vertex block's size both give a format, and they differ (never in a client
        /// file). The decoded arrays follow the size (the bytes); the caller reports the disagreement.</summary>
        public bool Disagrees;
    }

    public static class LiquidFormat
    {
        public const int None = -1, HeightDepth = 0, HeightUv = 1, DepthOnly = 2, HeightUvDepth = 3;
        /// <summary>The liquid type whose instances are depth-only or flat whatever its material says ("Ocean").</summary>
        public const int OceanType = 2;

        /// <summary>The single scale applied to stored texture coordinates (MH2O u16 u/v, MLIQ i16 s/t), whose own
        /// scale is UNKNOWN (09-liquids.md 1.2, 1.3: MH2O steps about 42.5 per quad and wraps at 65536). 1/256 is a
        /// documented choice, to be checked against the in-game references; 65536 x 1/256 is a whole number, so a
        /// wrap of the stored value does not move the texture.</summary>
        public const float StoredUvScale = 1f / 256f;

        public static int BytesPerVertex(int format)
        {
            switch (format)
            {
                case HeightDepth: return 5;
                case HeightUv: return 8;
                case DepthOnly: return 1;
                case HeightUvDepth: return 9;
                default: return 0;
            }
        }

        /// <summary>The format of a vertex block of `bytes` per vertex, -1 for any other size.</summary>
        public static int FormatOfBytesPerVertex(int bytes)
        {
            switch (bytes)
            {
                case 5: return HeightDepth;
                case 8: return HeightUv;
                case 1: return DepthOnly;
                case 9: return HeightUvDepth;
                default: return None;
            }
        }

        public static bool HasHeights(int format) { return format == HeightDepth || format == HeightUv || format == HeightUvDepth; }

        public static bool HasUv(int format) { return format == HeightUv || format == HeightUvDepth; }

        public static bool HasDepth(int format) { return format == HeightDepth || format == DepthOnly || format == HeightUvDepth; }

        /// <summary>Whether an MH2O instance's depth bytes are on the O scale: its second word is 2 or 42 (PROVEN_BYTES, see
        /// the file header); every other word is the W scale.</summary>
        public static bool DepthScaleO(ushort objectOrFormat) { return objectOrFormat == 2 || objectOrFormat == 42; }

        /// <summary>W scale (byte = clamp(floor(9 D), 0, 255)): the middle of the byte's step, (b + 0.5) / 9 yards.</summary>
        public static double DepthYardsW(byte b) { return (b + 0.5) / 9.0; }

        /// <summary>O scale (byte = clamp(ceil(12/7 D), 0, 255)): 0 for byte 0, else the middle of its step, (b - 0.5) x 7/12
        /// yards.</summary>
        public static double DepthYardsO(byte b) { return b == 0 ? 0.0 : (b - 0.5) * 7.0 / 12.0; }

        /// <summary>An MH2O depth byte in yards, on the scale its instance's second word picks.</summary>
        public static double DepthYards(byte b, ushort objectOrFormat) { return DepthScaleO(objectOrFormat) ? DepthYardsO(b) : DepthYardsW(b); }

        /// <summary>The format rule (09-liquids.md 1.2) for an MH2O instance: the second word if it is 0, 1 or 2;
        /// else 2 for liquid type 2; else the catalogue's LVF for the type; else (no catalogue row, or no material)
        /// the instance's FormatBySize. Disagrees is set when the rule and the size both give a format and they
        /// differ. `catalog` may be null.</summary>
        public static LiquidFormatDecision Decide(LiquidInstance instance, ILiquidCatalog catalog)
        {
            if (instance == null)
                throw new ArgumentNullException("instance");
            var d = new LiquidFormatDecision { Format = None, Source = LiquidFormatSource.None };
            LiquidTypeView type;
            if (instance.ObjectOrFormat <= 2)
            {
                d.Format = instance.ObjectOrFormat;
                d.Source = LiquidFormatSource.Word;
            }
            else if (instance.LiquidType == OceanType)
            {
                d.Format = DepthOnly;
                d.Source = LiquidFormatSource.OceanType;
            }
            else if (catalog != null && catalog.TryGetType(instance.LiquidType, out type) && type != null && type.Lvf >= 0 && type.Lvf <= 3)
            {
                d.Format = type.Lvf;
                d.Source = LiquidFormatSource.Catalog;
            }
            else if (instance.FormatBySize >= 0)
            {
                d.Format = instance.FormatBySize;
                d.Source = LiquidFormatSource.Size;
            }
            d.Disagrees = d.Source != LiquidFormatSource.Size && instance.FormatBySize >= 0 && d.Format != instance.FormatBySize;
            return d;
        }

        /// <summary>Decide's format: 0..3, or -1 when no rule applies and the instance has no vertex data.</summary>
        public static int Resolve(LiquidInstance instance, ILiquidCatalog catalog)
        {
            return Decide(instance, catalog).Format;
        }
    }

    /// <summary>
    /// One liquid mesh as plain arrays, for one liquid type: positions in the Unity frame (the builder says which
    /// origin), two uv channels, triangle indices with the front faces up (Unity winding, as the terrain's). The
    /// vertex attributes:
    ///   uv0 = texture coordinates: world-planar for water and ocean, or the stored u/v (s/t) scaled by
    ///         LiquidFormat.StoredUvScale (see the builders);
    ///   uv1 = (depth byte / 255, depth in yards: LiquidFormat.DepthYards) where the vertex has a stored depth byte, else
    ///         (1, -1). The shader's fallback reads x, its zone-light path y (Resources/WmvLiquid.shader).
    /// Vertices are shared inside one MH2O instance (or one MLIQ), never across instances or chunks.
    /// </summary>
    public sealed class LiquidSurface
    {
        public int LiquidType;                    // the LiquidType id every vertex belongs to
        public LiquidTypeView Type;               // its catalogue row, null when the catalogue lacks it
        public float[] Positions = new float[0];  // 3 per vertex
        public float[] Uv0 = new float[0];        // 2 per vertex
        public float[] Uv1 = new float[0];        // 2 per vertex
        public int[] Indices = new int[0];        // 6 per quad (two triangles)
        public int Instances;                     // MH2O instances (or 1 for an MLIQ) with at least one quad here
        public int PlanarUvInstances;             // of those, with world-planar uv0
        public int StoredUvInstances;             // of those, with stored u/v (s/t) as uv0
        public float[] BoundsMin, BoundsMax;      // over every vertex; null without vertices

        public int VertexCount { get { return Positions.Length / 3; } }
        public int QuadCount { get { return Indices.Length / 6; } }

        /// <summary>Upload bytes: 28 per vertex (position, uv0, uv1) plus the indices (16-bit up to 65,535
        /// vertices, 32-bit above).</summary>
        public long MeshBytes { get { return VertexCount * 28L + Indices.Length * (VertexCount > 65535 ? 4L : 2L); } }
    }

    /// <summary>Accumulates one LiquidSurface (shared by the MH2O and MLIQ builders).</summary>
    internal sealed class LiquidSurfaceBuilder
    {
        readonly List<float> positions = new List<float>();
        readonly List<float> uv0 = new List<float>();
        readonly List<float> uv1 = new List<float>();
        readonly List<int> indices = new List<int>();
        readonly float[] min = { float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity };
        readonly float[] max = { float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity };
        public readonly LiquidSurface Surface;

        public LiquidSurfaceBuilder(int liquidType, LiquidTypeView type)
        {
            Surface = new LiquidSurface { LiquidType = liquidType, Type = type };
        }

        public int VertexCount { get { return positions.Count / 3; } }

        public int AddVertex(float x, float y, float z, float u0, float v0, float u1, float v1)
        {
            positions.Add(x); positions.Add(y); positions.Add(z);
            uv0.Add(u0); uv0.Add(v0);
            uv1.Add(u1); uv1.Add(v1);
            Grow(0, x);
            Grow(1, y);
            Grow(2, z);
            return positions.Count / 3 - 1;
        }

        void Grow(int axis, float v)
        {
            if (v < min[axis]) min[axis] = v;
            if (v > max[axis]) max[axis] = v;
        }

        public void AddTriangle(int a, int b, int c)
        {
            indices.Add(a); indices.Add(b); indices.Add(c);
        }

        public LiquidSurface Finish()
        {
            Surface.Positions = positions.ToArray();
            Surface.Uv0 = uv0.ToArray();
            Surface.Uv1 = uv1.ToArray();
            Surface.Indices = indices.ToArray();
            if (positions.Count > 0)
            {
                Surface.BoundsMin = (float[])min.Clone();
                Surface.BoundsMax = (float[])max.Clone();
            }
            return Surface;
        }
    }
}
