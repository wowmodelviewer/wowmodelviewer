// TerrainDump.cs
//
// The one writer of the terrain mesh dump: the console runner dumps the builder's output
// (TerrainMeshBuilder, WMV_TERRAIN_DUMP_DIR) and the player dumps the arrays it reads back from its
// Unity meshes (-wmvTerrainDump); Tools/world-audit/compare_terrain_mesh.py compares either with the
// independent Python reference (terrain_reference.py writes the same format). Unity-free: the player
// fills a TerrainDumpTile and writes the text. Every float is written as its IEEE-754 bit pattern,
// so values round-trip exactly (including -0.0) whatever the culture or runtime formats numbers like.
//
// Format, version 1: ASCII text, one record per line, fields separated by one space, "\n" line ends.
//   wmv-terrain-dump 1
//   source <runner|player|reference>
//   tile <tileX> <tileY>
//   origin <X> <Y> <Z>                     the world point (game frame, yards) Unity (0,0,0) stands for:
//                                          double bit patterns, 16 hex digits each
//   counts <chunks> <built> <skipped> <unsupported> <vertices> <triangles> <holeCells>
//   bounds <minX> <minY> <minZ> <maxX> <maxY> <maxZ>  |  bounds none
//                                          float bit patterns, 8 hex digits each (Unity, root-local)
//   meshbytes <n>
//   meta <key> <value>                     zero or more; the value is the rest of the line
//   root <layer> <active> <m00> <m01> <m02> <m03> <m10> ... <m33>
//                                          player only: the tile root's layer, activeInHierarchy (0/1)
//                                          and localToWorld, row-major, float bits
//   then for each chunk n = 0..255:
//   chunk <n> <ix> <iy> <state> <holeMask> <holeCells> <triangles>
//                                          state Built|Skipped|Unsupported; holeMask 16 hex digits
//   reason <text>                          when the chunk has one (rest of the line)
//   pos <435 values>                       x y z per MCVT entry k (Unity, origin-local), float bits
//   nrm <435 values>                       geometry-derived diagnostic normals, float bits
//   idx <3 * triangles values>             decimal MCVT indices, Unity index order
//   cbounds <6 values>  |  cbounds none    referenced-vertex bounds, float bits
//   unity <layer> <active> <subMeshCount> <topology> <indexFormat> <bmin x y z> <bmax x y z> <m00> ... <m33>
//                                          player only: the chunk object's layer, activeInHierarchy,
//                                          the mesh's sub-mesh count, topology, index format, bounds
//                                          (min/max) and the object's localToWorld (row-major)
//   end
// pos/nrm/idx/cbounds are written for every chunk that carries arrays: the runner writes them for every
// Built chunk; the player only for the chunks it made a mesh for (chunks with triangles). Chunks
// without arrays have only their chunk (and reason) line.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace Wmv.Wow.World
{
    /// <summary>Unity-side facts of one object (player dumps only).</summary>
    public sealed class TerrainDumpUnity
    {
        public int Layer;
        public bool ActiveInHierarchy;
        public int SubMeshCount;            // chunk objects only
        public string Topology;             // e.g. "Triangles"
        public string IndexFormat;          // e.g. "UInt16"
        public float[] MeshBoundsMin, MeshBoundsMax;
        public float[] LocalToWorld;        // 16 values, row-major (m00 m01 m02 m03 m10 ...)
    }

    public sealed class TerrainDumpChunk
    {
        public int Index, IndexX, IndexY;
        public TerrainChunkState State;
        public string Reason;
        public ulong HoleMask;
        public int HoleCells, Triangles;
        public float[] Positions, Normals;  // 435 each, or null (not written)
        public int[] Indices;               // null = not written
        public float[] BoundsMin, BoundsMax;
        public TerrainDumpUnity Unity;      // null except in a player dump
    }

    public sealed class TerrainDumpTile
    {
        public string Source;
        public int TileX, TileY;
        public WorldOrigin Origin;
        public int ChunkCount, ChunksBuilt, ChunksSkipped, ChunksUnsupported, Vertices, Triangles, HoleCells;
        public float[] BoundsMin, BoundsMax;
        public long MeshBytes;
        public readonly List<KeyValuePair<string, string>> Meta = new List<KeyValuePair<string, string>>();
        public TerrainDumpUnity Root;       // player only (layer, activeInHierarchy, localToWorld)
        public TerrainDumpChunk[] Chunks;
    }

    public static class TerrainDump
    {
        public const string Magic = "wmv-terrain-dump";
        public const int FormatVersion = 1;

        /// <summary>A dump of the builder's output; every array is copied, so the caller may replace
        /// a chunk's arrays with what it read back from its own meshes.</summary>
        public static TerrainDumpTile FromMesh(TerrainTileMesh mesh, string source)
        {
            if (mesh == null)
                throw new ArgumentNullException("mesh");
            var t = new TerrainDumpTile
            {
                Source = source, TileX = mesh.TileX, TileY = mesh.TileY, Origin = mesh.Origin, ChunkCount = mesh.Chunks.Length,
                ChunksBuilt = mesh.ChunksBuilt, ChunksSkipped = mesh.ChunksSkipped, ChunksUnsupported = mesh.ChunksUnsupported,
                Vertices = mesh.Vertices, Triangles = mesh.Triangles, HoleCells = mesh.HoleCells,
                BoundsMin = Copy(mesh.BoundsMin), BoundsMax = Copy(mesh.BoundsMax), MeshBytes = mesh.MeshBytes,
                Chunks = new TerrainDumpChunk[mesh.Chunks.Length],
            };
            for (int n = 0; n < mesh.Chunks.Length; n++)
            {
                TerrainChunkMesh c = mesh.Chunks[n];
                int[] idx = null;
                if (c.State == TerrainChunkState.Built)
                {
                    idx = new int[c.Indices.Length];
                    for (int i = 0; i < idx.Length; i++) idx[i] = c.Indices[i];
                }
                t.Chunks[n] = new TerrainDumpChunk
                {
                    Index = c.Index, IndexX = c.IndexX, IndexY = c.IndexY, State = c.State, Reason = c.Reason, HoleMask = c.HoleMask,
                    HoleCells = c.HoleCells, Triangles = c.Triangles, Positions = Copy(c.Positions), Normals = Copy(c.Normals), Indices = idx,
                    BoundsMin = Copy(c.BoundsMin), BoundsMax = Copy(c.BoundsMax),
                };
            }
            return t;
        }

        public static string ToText(TerrainDumpTile tile)
        {
            var sw = new StringWriter(CultureInfo.InvariantCulture);
            Write(tile, sw);
            return sw.ToString();
        }

        public static void Write(TerrainDumpTile tile, TextWriter w)
        {
            if (tile == null)
                throw new ArgumentNullException("tile");
            var sb = new StringBuilder(1 << 16);
            Line(w, sb.Clear().Append(Magic).Append(' ').Append(Int(FormatVersion)));
            Line(w, sb.Clear().Append("source ").Append(Word(tile.Source ?? "unknown")));
            Line(w, sb.Clear().Append("tile ").Append(Int(tile.TileX)).Append(' ').Append(Int(tile.TileY)));
            Line(w, sb.Clear().Append("origin ").Append(D(tile.Origin.X)).Append(' ').Append(D(tile.Origin.Y)).Append(' ').Append(D(tile.Origin.Z)));
            Line(w, sb.Clear().Append("counts ").Append(Int(tile.ChunkCount)).Append(' ').Append(Int(tile.ChunksBuilt)).Append(' ')
                       .Append(Int(tile.ChunksSkipped)).Append(' ').Append(Int(tile.ChunksUnsupported)).Append(' ').Append(Int(tile.Vertices))
                       .Append(' ').Append(Int(tile.Triangles)).Append(' ').Append(Int(tile.HoleCells)));
            sb.Clear().Append("bounds");
            AppendBounds(sb, tile.BoundsMin, tile.BoundsMax);
            Line(w, sb);
            Line(w, sb.Clear().Append("meshbytes ").Append(tile.MeshBytes.ToString(CultureInfo.InvariantCulture)));
            foreach (KeyValuePair<string, string> kv in tile.Meta)
                Line(w, sb.Clear().Append("meta ").Append(Word(kv.Key)).Append(' ').Append(Text(kv.Value)));
            if (tile.Root != null)
            {
                sb.Clear().Append("root ").Append(Int(tile.Root.Layer)).Append(' ').Append(tile.Root.ActiveInHierarchy ? '1' : '0');
                AppendFloats(sb, tile.Root.LocalToWorld, 16, "root localToWorld");
                Line(w, sb);
            }
            if (tile.Chunks != null)
                foreach (TerrainDumpChunk c in tile.Chunks)
                {
                    if (c == null)
                        continue;
                    Line(w, sb.Clear().Append("chunk ").Append(Int(c.Index)).Append(' ').Append(Int(c.IndexX)).Append(' ').Append(Int(c.IndexY))
                               .Append(' ').Append(c.State.ToString()).Append(' ').Append(c.HoleMask.ToString("x16", CultureInfo.InvariantCulture))
                               .Append(' ').Append(Int(c.HoleCells)).Append(' ').Append(Int(c.Triangles)));
                    if (c.Reason != null)
                        Line(w, sb.Clear().Append("reason ").Append(Text(c.Reason)));
                    bool arrays = c.Positions != null || c.Normals != null || c.Indices != null;
                    if (c.Positions != null)
                    {
                        sb.Clear().Append("pos");
                        AppendFloats(sb, c.Positions, TerrainMeshBuilder.VerticesPerChunk * 3, "positions");
                        Line(w, sb);
                    }
                    if (c.Normals != null)
                    {
                        sb.Clear().Append("nrm");
                        AppendFloats(sb, c.Normals, TerrainMeshBuilder.VerticesPerChunk * 3, "normals");
                        Line(w, sb);
                    }
                    if (c.Indices != null)
                    {
                        sb.Clear().Append("idx");
                        foreach (int i in c.Indices)
                            sb.Append(' ').Append(Int(i));
                        Line(w, sb);
                    }
                    if (arrays)
                    {
                        sb.Clear().Append("cbounds");
                        AppendBounds(sb, c.BoundsMin, c.BoundsMax);
                        Line(w, sb);
                    }
                    if (c.Unity != null)
                    {
                        TerrainDumpUnity u = c.Unity;
                        sb.Clear().Append("unity ").Append(Int(u.Layer)).Append(' ').Append(u.ActiveInHierarchy ? '1' : '0').Append(' ')
                          .Append(Int(u.SubMeshCount)).Append(' ').Append(Word(u.Topology ?? "unknown")).Append(' ').Append(Word(u.IndexFormat ?? "unknown"));
                        AppendFloats(sb, u.MeshBoundsMin, 3, "mesh bounds min");
                        AppendFloats(sb, u.MeshBoundsMax, 3, "mesh bounds max");
                        AppendFloats(sb, u.LocalToWorld, 16, "chunk localToWorld");
                        Line(w, sb);
                    }
                }
            Line(w, sb.Clear().Append("end"));
        }

        static void Line(TextWriter w, StringBuilder sb)
        {
            sb.Append('\n');
            w.Write(sb.ToString());
        }

        static float[] Copy(float[] a) { return a == null ? null : (float[])a.Clone(); }

        static string Int(int v) { return v.ToString(CultureInfo.InvariantCulture); }

        static string D(double v) { return ((ulong)BitConverter.DoubleToInt64Bits(v)).ToString("x16", CultureInfo.InvariantCulture); }

        static void AppendFloat(StringBuilder sb, float v)
        {
            sb.Append(((uint)BitConverter.SingleToInt32Bits(v)).ToString("x8", CultureInfo.InvariantCulture));
        }

        static void AppendFloats(StringBuilder sb, float[] values, int count, string what)
        {
            if (values == null || values.Length != count)
                throw new ArgumentException(string.Format("{0}: {1} value(s) expected, got {2}", what, count, values == null ? 0 : values.Length));
            foreach (float v in values)
            {
                sb.Append(' ');
                AppendFloat(sb, v);
            }
        }

        static void AppendBounds(StringBuilder sb, float[] min, float[] max)
        {
            if (min == null || max == null)
            {
                sb.Append(" none");
                return;
            }
            AppendFloats(sb, min, 3, "bounds min");
            AppendFloats(sb, max, 3, "bounds max");
        }

        /// <summary>One field: printable ASCII without spaces.</summary>
        static string Word(string s)
        {
            string t = Text(s).Replace(' ', '_');
            return t.Length == 0 ? "-" : t;
        }

        /// <summary>Rest-of-line text: printable ASCII only (anything else becomes '?', line breaks a space).</summary>
        static string Text(string s)
        {
            if (s == null)
                return "";
            var b = new StringBuilder(s.Length);
            foreach (char ch in s)
            {
                if (ch == '\r' || ch == '\n' || ch == '\t') b.Append(' ');
                else if (ch < 32 || ch > 126) b.Append('?');
                else b.Append(ch);
            }
            return b.ToString();
        }
    }
}
