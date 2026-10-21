// TerrainLiquidMesh.cs
//
// Mesh data for the MH2O liquid of one root ADT tile: per liquid type, one LiquidSurface holding the existing
// quads of every instance and layer of the tile. Pure C#, no UnityEngine: the player turns the arrays into Unity
// meshes; the console runner checks them (Tests/LiquidTests.cs, LiquidCorpusTests.cs).
//
// Geometry (docs/world-viewer/09-liquids.md 1.2 and 1.7, PROVEN_BYTES by seam tests). Vertex (i, j) of an instance
// at cells (X, Y) of chunk (ix, iy) is the chunk's outer terrain vertex (row Y + i, column X + j): its world x/y are
// TerrainMeshBuilder.VertexWorldXY of MCVT entry Outer(Y + i, X + j) -- the terrain's own expressions, so a liquid
// vertex lies bit-exactly over the terrain's grid point -- and its z is the vertex's height (HeightAt: the stored
// height, or the instance's min for depth-only data and for an instance without vertex data). Positions go through
// WorldToUnity with the caller's origin, as the terrain's do. A quad (r, c) that exists makes two triangles over
// a = (r, c), b = (r, c + 1), cc = (r + 1, c), d = (r + 1, c + 1): (a, b, d) and (a, d, cc), front faces up like
// the terrain's cells (Cross.y = U^2 for each). Which diagonal the game splits a liquid quad on is UNKNOWN; this one
// is a choice. Vertices are shared inside an instance (only those an existing quad uses are emitted), never across
// instances or chunks: seams follow the data (same-type liquid matches at 99.6-100% of seams within 0.01 yd).
//
// Texture coordinates (uv0; 09-liquids.md 1.5, an approximation):
//   - water and ocean kinds: world-planar, on the game's own axes. (u, v) = (X, Y) x 2 r / C in the game's world frame
//     (X north, Y west; C = 1600/3/16 yd, a chunk; r = the type's Float0, "texture tiles per block", INFERRED): twice
//     the type's repeats (PlanarRepeatFactor). The factor and the axes are MEASURED at one spot for type 5 (Southfury
//     River: the ripple density, blob size and autocorrelation fit x2, the streaks' orientation fits these axes; the 180
//     degree sense is UNKNOWN), and the factor matches an editor's rule in public format documentation (one repeat per
//     4 liquid vertices); for every other type both are INFERRED. Turned by Float1 degrees ("rotation", INFERRED; from
//     u towards v, counter-clockwise seen from above, is a choice). Computed in double from the world position, then
//     the floor of the instance's reference vertex (the first corner of its first existing quad) is subtracted from
//     every vertex of the instance, so the floats stay small and the floating origin never moves the pattern (a
//     whole-number shift is invisible to a repeating texture).
//   - other kinds whose vertex data stores u/v (formats 1 and 3): the stored u16 values times
//     LiquidFormat.StoredUvScale (scale UNKNOWN). The u16 values wrap at 65536: each vertex is taken as its signed
//     16-bit step from the reference vertex, which joins a wrap inside an instance (the corpus's largest distance from
//     a reference is 1,046 steps, far below 32,768; Tests/LiquidCorpusTests.cs), and the floor of the reference's
//     value is subtracted as above.
//   - other kinds without stored u/v: world-planar, as water.
//   - a type the catalogue lacks: stored u/v when the data has them, else world-planar with DefaultRepeatsPerChunk
//     and no rotation (a non-finite Float0 or Float1 is replaced the same way).
// uv1 = (depth byte / 255, depth in yards) at a vertex with a stored depth byte (formats 0, 2, 3), else (1, -1): the
// yards decoded on the scale the instance's second word picks (LiquidFormat.DepthYards: 2 or 42 the O scale, any other
// the W scale; PROVEN_BYTES). The shader's fallback reads x (unchanged), its zone-light path y.
//
// Formats: the arrays come from the parser, which reads each vertex block by its size (the bytes). The format rule
// (LiquidFormat.Decide) is evaluated per instance with vertex data; a decision taken from the size (no catalogue row)
// and a disagreement between the rule and the size are counted for the report, and the geometry follows the bytes.
// A Malformed instance has no geometry and is counted. Thread safety: no static state; Build may run on any thread.

using System;
using System.Collections.Generic;

namespace Wmv.Wow.World
{
    /// <summary>A tile's MH2O as liquid meshes plus the counts the player reports.</summary>
    public sealed class TerrainLiquidTile
    {
        public int TileX, TileY;
        public WorldOrigin Origin;                // the world point Unity (0, 0, 0) stands for
        /// <summary>One surface per liquid type with at least one quad, in first-use order (chunk order, then layer).</summary>
        public List<LiquidSurface> Surfaces = new List<LiquidSurface>();
        public int ChunksWithLiquid;              // chunks with at least one instance
        public int Instances;                     // every instance record (layers)
        public int MultiLayerChunks;              // chunks with more than one instance
        public int MaxLayers;                     // the most instances in one chunk
        public int InstancesWithQuads;            // decoded instances with at least one existing quad
        public int MalformedInstances;            // not drawn (the parser's issues say why)
        public int Quads, Vertices;
        public int FormatFromSize;                // instances with vertex data whose format the catalogue could not give
        public int FormatDisagreements;           // instances whose rule and size give different formats
        /// <summary>Distinct liquid type ids of the tile's instances (drawn or not), in first-use order.</summary>
        public List<int> Types = new List<int>();
        public int MissingTypes;                  // of those, types the catalogue lacks (all of them without one)
        public long GeometryBytes;                // sum of the surfaces' MeshBytes
    }

    public static class TerrainLiquidMesh
    {
        /// <summary>Texture repeats per chunk for a type the catalogue lacks (a documented default, no rotation).</summary>
        public const double DefaultRepeatsPerChunk = 1.0;
        /// <summary>Planar uv repeats this many times the type's Float0 per chunk: MEASURED at one spot for type 5 (Slow Water;
        /// x1.75 fits too sparse, x2.25 too dense), INFERRED for every other type (see the file header).</summary>
        public const double PlanarRepeatFactor = 2.0;

        /// <summary>Builds the tile's liquid surfaces. `liquids` may be null (no MH2O) or Malformed (no instances);
        /// `catalog` may be null (every type is then missing). Positions are relative to `origin`, like the
        /// terrain mesh's.</summary>
        public static TerrainLiquidTile Build(AdtLiquids liquids, int tileX, int tileY, WorldOrigin origin, ILiquidCatalog catalog)
        {
            if (tileX < 0 || tileX >= TerrainCoordinates.TilesPerSide || tileY < 0 || tileY >= TerrainCoordinates.TilesPerSide)
                throw new ArgumentOutOfRangeException("tile", "tile x/y must be 0..63");
            var tile = new TerrainLiquidTile { TileX = tileX, TileY = tileY, Origin = origin };
            if (liquids == null || liquids.Instances == null)
                return tile;
            if (liquids.Instances.Length != AdtParser.ChunksPerTile)
                throw new ArgumentException(string.Format("MH2O has 256 chunk entries, this one has {0}", liquids.Instances.Length), "liquids");

            var builders = new Dictionary<int, LiquidSurfaceBuilder>();
            var order = new List<int>();
            var views = new Dictionary<int, LiquidTypeView>();
            for (int n = 0; n < AdtParser.ChunksPerTile; n++)
            {
                LiquidInstance[] layers = liquids.Instances[n];
                if (layers == null || layers.Length == 0)
                    continue;
                tile.ChunksWithLiquid++;
                tile.Instances += layers.Length;
                if (layers.Length > 1)
                    tile.MultiLayerChunks++;
                tile.MaxLayers = Math.Max(tile.MaxLayers, layers.Length);
                foreach (LiquidInstance li in layers)
                {
                    LiquidTypeView type;
                    if (!views.TryGetValue(li.LiquidType, out type))
                    {
                        if (catalog == null || !catalog.TryGetType(li.LiquidType, out type))
                            type = null;
                        views[li.LiquidType] = type;
                        tile.Types.Add(li.LiquidType);
                        if (type == null)
                            tile.MissingTypes++;
                    }
                    if (li.Status != WorldDataStatus.Decoded)
                    {
                        tile.MalformedInstances++;
                        continue;
                    }
                    if (li.FormatBySize >= 0)
                    {
                        LiquidFormatDecision d = LiquidFormat.Decide(li, catalog);
                        if (d.Source == LiquidFormatSource.Size)
                            tile.FormatFromSize++;
                        if (d.Disagrees)
                            tile.FormatDisagreements++;
                    }
                    LiquidSurfaceBuilder b;
                    if (!builders.TryGetValue(li.LiquidType, out b))
                    {
                        b = new LiquidSurfaceBuilder(li.LiquidType, type);
                        builders[li.LiquidType] = b;
                        order.Add(li.LiquidType);
                    }
                    if (AddInstance(b, li, type, tileX, tileY, n % 16, n / 16, origin) > 0)
                        tile.InstancesWithQuads++;
                }
            }
            foreach (int id in order)
            {
                LiquidSurface s = builders[id].Finish();
                if (s.QuadCount == 0)
                    continue;
                tile.Surfaces.Add(s);
                tile.Quads += s.QuadCount;
                tile.Vertices += s.VertexCount;
                tile.GeometryBytes += s.MeshBytes;
            }
            return tile;
        }

        /// <summary>Appends one decoded instance's existing quads to `b`; returns the quads added.</summary>
        static int AddInstance(LiquidSurfaceBuilder b, LiquidInstance li, LiquidTypeView type, int tileX, int tileY, int chunkX, int chunkY, WorldOrigin origin)
        {
            int w = li.Width, h = li.Height;
            int first = Array.IndexOf(li.Exists, true);
            if (first < 0)
                return 0;
            var index = new int[(w + 1) * (h + 1)];
            for (int k = 0; k < index.Length; k++)
                index[k] = -1;
            bool stored = li.U != null && li.V != null && (type == null || !type.IsWaterOrOcean);
            double repeats = DefaultRepeatsPerChunk, degrees = 0;
            if (type != null)
            {
                repeats = Finite(type.Float0) ? type.Float0 : DefaultRepeatsPerChunk;
                degrees = Finite(type.Float1) ? type.Float1 : 0;
            }

            // The reference vertex: the first corner of the first existing quad (an unused vertex may hold leftovers).
            var at = new UvFrame { Instance = li, Stored = stored, Repeats = repeats, Degrees = degrees };
            int r0 = first / w, c0 = first % w;
            at.Reference = r0 * (w + 1) + c0;
            double x0, y0, u0, v0;
            TerrainMeshBuilder.VertexWorldXY(tileX, tileY, chunkX, chunkY, TerrainVertexLayout.Outer(li.Y + r0, li.X + c0), out x0, out y0);
            at.Uv(x0, y0, at.Reference, out u0, out v0);
            at.ShiftU = Math.Floor(u0);
            at.ShiftV = Math.Floor(v0);

            int quads = 0;
            for (int r = 0; r < h; r++)
                for (int c = 0; c < w; c++)
                {
                    if (!li.Exists[r * w + c])
                        continue;
                    int a = Vertex(b, at, index, r, c, tileX, tileY, chunkX, chunkY, origin);
                    int bb = Vertex(b, at, index, r, c + 1, tileX, tileY, chunkX, chunkY, origin);
                    int cc = Vertex(b, at, index, r + 1, c, tileX, tileY, chunkX, chunkY, origin);
                    int d = Vertex(b, at, index, r + 1, c + 1, tileX, tileY, chunkX, chunkY, origin);
                    b.AddTriangle(a, bb, d);
                    b.AddTriangle(a, d, cc);
                    quads++;
                }
            if (quads > 0)
            {
                b.Surface.Instances++;
                if (stored) b.Surface.StoredUvInstances++;
                else b.Surface.PlanarUvInstances++;
            }
            return quads;
        }

        /// <summary>How one instance's uv0 is made: the source, the planar parameters, the reference vertex and the
        /// whole-number shift taken from it.</summary>
        sealed class UvFrame
        {
            public LiquidInstance Instance;
            public bool Stored;
            public double Repeats, Degrees;
            public int Reference;
            public double ShiftU, ShiftV;

            /// <summary>uv0 before the shift of instance vertex k at world (x, y).</summary>
            public void Uv(double x, double y, int k, out double u, out double v)
            {
                if (Stored)
                {
                    u = StoredStep(Instance.U[Reference], Instance.U[k]) * LiquidFormat.StoredUvScale;
                    v = StoredStep(Instance.V[Reference], Instance.V[k]) * LiquidFormat.StoredUvScale;
                }
                else
                    PlanarUv(x, y, Repeats, Degrees, out u, out v);
            }
        }

        /// <summary>The surface index of instance vertex (i, j), emitting it on first use.</summary>
        static int Vertex(LiquidSurfaceBuilder b, UvFrame at, int[] index, int i, int j, int tileX, int tileY, int chunkX, int chunkY, WorldOrigin origin)
        {
            LiquidInstance li = at.Instance;
            int k = i * (li.Width + 1) + j;
            if (index[k] >= 0)
                return index[k];
            double x, y;
            TerrainMeshBuilder.VertexWorldXY(tileX, tileY, chunkX, chunkY, TerrainVertexLayout.Outer(li.Y + i, li.X + j), out x, out y);
            float ux, uy, uz;
            WorldToUnity.Position(x, y, li.HeightAt(i, j), origin, out ux, out uy, out uz);
            double u, v;
            at.Uv(x, y, k, out u, out v);
            float d1 = 1f, d2 = -1f;
            if (li.Depths != null)
            {
                d1 = li.Depths[k] / 255f;
                d2 = (float)LiquidFormat.DepthYards(li.Depths[k], li.ObjectOrFormat);
            }
            index[k] = b.AddVertex(ux, uy, uz, (float)(u - at.ShiftU), (float)(v - at.ShiftV), d1, d2);
            return index[k];
        }

        /// <summary>A stored u16 coordinate as the reference value plus its signed 16-bit step from it: joins a wrap
        /// of the stored value between the reference and this vertex (an instance's steps stay far below 32,768).</summary>
        static double StoredStep(ushort reference, ushort value)
        {
            return reference + (double)unchecked((short)(ushort)(value - reference));
        }

        /// <summary>World-planar texture coordinates of the game-frame point (x north, y west): (x, y) in chunks, times
        /// PlanarRepeatFactor x `repeatsPerChunk`, turned by `degrees` (from u towards v: counter-clockwise seen from above).</summary>
        public static void PlanarUv(double x, double y, double repeatsPerChunk, double degrees, out double u, out double v)
        {
            double s = PlanarRepeatFactor * repeatsPerChunk / TerrainCoordinates.ChunkSize;
            double u0 = x * s, v0 = y * s;
            double a = degrees * (Math.PI / 180.0);
            double cs = Math.Cos(a), sn = Math.Sin(a);
            u = u0 * cs - v0 * sn;
            v = u0 * sn + v0 * cs;
        }

        static bool Finite(float f) { return !float.IsNaN(f) && !float.IsInfinity(f); }
    }
}
