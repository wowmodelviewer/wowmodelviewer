// LiquidCorpusTests.cs
//
// The Stage 09 liquid layer on REAL client bytes (docs/world-viewer/09-liquids.md 1.2, 1.3, 1.10). Runs only from the
// console runner (file IO, JSON, hashing).
//
//   (a) Every root ADT of the pinned audit corpus (WMV_WORLD_AUDIT_CORPUS; inputs checked against
//       Tools/world-audit/tests/corpus_hashes.json), four clients: every MH2O instance decodes with no issue; its
//       format by size equals the rule's where the instance alone decides it (second word 0, 1 or 2: that format or
//       no data; liquid type 2: format 2 or no data), and is a valid size (1, 5, 8 or 9 bytes per vertex) otherwise;
//       every height an existing quad uses lies in the instance's [min, max]; the tile's liquid mesh has exactly the
//       existing quads and the vertices they use, every triangle facing up. Instance totals are pinned (the corpus
//       results the audit recorded).
//   (b) The roadmap's exit tiles, in the same pinned corpus: Retail 29_48 (root 777427: 229 instances, all type 2
//       "Ocean", word 42, flat at 0.0, 136 depth-only and 93 without data, 24 bitmaps) and Era 36_49 (root 778837:
//       132 instances, all type 5 "Slow Water", object 5987, format 0, flat at 56.1615, 50 bitmaps, 132 attribute
//       blocks), the Era chunk-72 byte walkthrough (09-liquids investigation), exact mesh counts, the liquid vertices
//       of a known chunk on the terrain's grid, and the seams between neighbouring chunks.
//   (c) The investigation's MLIQ examples (WMV_LIQUID_EXAMPLES = the folder holding manifest.json and
//       <client>\<FileDataID>.bin; otherwise SKIPPED): the files against the manifest (itself pinned), then per group
//       the census's figures (X, Y, A, B, corner, material, size, present tiles, nibble, used-height range), the
//       vertex payload pattern, the group liquid type rule, and the group-frame mesh.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class LiquidCorpusTests
    {
        static readonly string[] Clients = { "wow", "wow_classic_era", "wow_classic", "wow_classic_beta" };

        /// <summary>MH2O instances of each client's corpus window (the audit's recorded C# parse,
        /// results/csharp_parsers.json, `clients/*/liquids`).</summary>
        static readonly Dictionary<string, int> WindowInstances = new Dictionary<string, int>
        {
            { "wow", 3109 }, { "wow_classic_era", 1793 }, { "wow_classic", 423 }, { "wow_classic_beta", 336 },
        };

        /// <summary>SHA-256 of the MLIQ examples' manifest.json (D:\wmv-09\inv\mliq-bytes\examples, 2026-10-18), which pins
        /// every example file in turn.</summary>
        const string ExamplesManifestSha256 = "6e1141d038f8cc42a97db7d1e1df3f1880b4d8008f21163e082365bd477e5a08";

        static int fails, passes;
        static Action<string> say;

        static void Verdict(bool ok, string name, string detail = null)
        {
            if (ok) passes++; else fails++;
            say((ok ? "  PASS  " : "  FAIL  ") + name + (detail != null ? " (" + detail + ")" : ""));
        }

        static string F(double v) { return v.ToString("0.####", CultureInfo.InvariantCulture); }

        public static int Run(string corpus, string examples, Action<string> output)
        {
            say = output;
            fails = 0;
            passes = 0;
            string pinsPath = TerrainCorpusTests.FindPins();
            if (pinsPath == null)
            {
                Verdict(false, "pins: Tools/world-audit/tests/corpus_hashes.json not found");
                return Finish();
            }
            TerrainCorpusTests.Pins pins = TerrainCorpusTests.LoadPins(pinsPath);
            foreach (string client in Clients)
                Window(corpus, pins, client);
            ExitTiles(corpus, pins);
            if (string.IsNullOrEmpty(examples))
                say("  SKIP  MLIQ examples: WMV_LIQUID_EXAMPLES is not set (the investigation's example files are local, not committed; not a pass)");
            else
                MliqExamples(examples);
            return Finish();
        }

        static int Finish()
        {
            say(string.Format("LiquidCorpusTests: {0} passed, {1} failed", passes, fails));
            return fails;
        }

        // ================================================================================== (a) corpus window

        sealed class RootFile
        {
            public uint Fid;
            public int X, Y;
        }

        static List<RootFile> WindowRoots(string corpus, string client)
        {
            var list = new List<RootFile>();
            using (JsonDocument doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(corpus, "corpus", client, "azeroth", "corpus.json"))))
                foreach (JsonElement f in doc.RootElement.GetProperty("files").EnumerateArray())
                    if (f.GetProperty("what").GetString() == "root" && f.GetProperty("status").GetString() == "OK")
                        list.Add(new RootFile { Fid = f.GetProperty("fdid").GetUInt32(), X = f.GetProperty("x").GetInt32(), Y = f.GetProperty("y").GetInt32() });
            return list;
        }

        static byte[] ReadPinned(string corpus, TerrainCorpusTests.Pins pins, string rel, out string problem)
        {
            problem = TerrainCorpusTests.CheckPinned(corpus, pins, rel, say);
            return problem == null ? File.ReadAllBytes(Path.Combine(corpus, rel)) : null;
        }

        /// <summary>Whether an existing quad of the instance uses vertex (i, j).</summary>
        static bool Used(LiquidInstance li, int i, int j)
        {
            return li.QuadExists(i - 1, j - 1) || li.QuadExists(i - 1, j) || li.QuadExists(i, j - 1) || li.QuadExists(i, j);
        }

        /// <summary>Smallest Cross(p1 - p0, p2 - p0).y over the surface's triangles.</summary>
        static double MinCrossY(LiquidSurface s)
        {
            double min = double.PositiveInfinity;
            float[] p = s.Positions;
            for (int t = 0; t < s.Indices.Length; t += 3)
            {
                int a = 3 * s.Indices[t], b = 3 * s.Indices[t + 1], c = 3 * s.Indices[t + 2];
                min = Math.Min(min, (double)(p[b + 2] - p[a + 2]) * (p[c] - p[a]) - (double)(p[b] - p[a]) * (p[c + 2] - p[a + 2]));
            }
            return min;
        }

        static void Window(string corpus, TerrainCorpusTests.Pins pins, string client)
        {
            say("-- liquids, " + client + " corpus window");
            List<RootFile> roots = WindowRoots(corpus, client);
            int withMh2o = 0, instances = 0, malformed = 0, issues = 0, decidable = 0, decidableOk = 0, undecided = 0, usedHeights = 0, outside = 0;
            int pinProblems = 0, meshProblems = 0, quadsAll = 0, uvWraps = 0, uvInstances = 0, maxQuadStep = 0, maxRefStep = 0;
            string maxQuadStepAt = "none";
            double maxUvExtent = 0;
            var bySize = new SortedDictionary<int, int>();
            var parseMs = new List<double>();
            var buildMs = new List<double>();
            foreach (RootFile rf in roots)
            {
                string problem;
                byte[] bytes = ReadPinned(corpus, pins, "corpus/" + client + "/azeroth/" + rf.Fid + ".bin", out problem);
                if (bytes == null)
                {
                    pinProblems++;
                    say("    " + problem);
                    continue;
                }
                var sw = Stopwatch.StartNew();
                AdtRoot root = AdtParser.ParseRoot(bytes, rf.X, rf.Y, client + " root " + rf.Fid);
                parseMs.Add(sw.Elapsed.TotalMilliseconds);
                if (root.Liquids == null)
                    continue;
                withMh2o++;
                issues += root.Issues.Count(i => i.Chunk != null && i.Chunk.StartsWith("MH2O", StringComparison.Ordinal));
                int quads = 0, vertices = 0;
                foreach (LiquidInstance[] layers in root.Liquids.Instances ?? new LiquidInstance[0][])
                    foreach (LiquidInstance li in layers)
                    {
                        instances++;
                        if (li.Status != WorldDataStatus.Decoded)
                        {
                            malformed++;
                            continue;
                        }
                        int key = li.FormatBySize;
                        bySize[key] = bySize.TryGetValue(key, out int c) ? c + 1 : 1;
                        if (li.ObjectOrFormat <= 2 || li.LiquidType == LiquidFormat.OceanType)
                        {
                            decidable++;
                            int rule = li.ObjectOrFormat <= 2 ? li.ObjectOrFormat : LiquidFormat.DepthOnly;
                            if (li.FormatBySize == rule || li.FormatBySize == -1)
                                decidableOk++;
                        }
                        else
                            undecided++;
                        for (int i = 0; i <= li.Height; i++)
                            for (int j = 0; j <= li.Width; j++)
                            {
                                if (!Used(li, i, j))
                                    continue;
                                vertices++;
                                usedHeights++;
                                float z = li.HeightAt(i, j);
                                if (!(z >= li.MinHeight && z <= li.MaxHeight))
                                    outside++;
                            }
                        quads += li.Exists.Count(e => e);
                        if (li.U != null)
                        {
                            uvInstances++;
                            if (StoredUvWraps(li))
                                uvWraps++;
                            maxRefStep = Math.Max(maxRefStep, MaxReferenceStep(li));
                            int step = MaxQuadStep(li);
                            if (step > maxQuadStep)
                            {
                                maxQuadStep = step;
                                maxQuadStepAt = string.Format("{0}_{1} chunk {2} layer {3}, type {4}, word {5}, format {6}", rf.X, rf.Y, li.Chunk, li.Layer,
                                                              li.LiquidType, li.ObjectOrFormat, li.FormatBySize);
                            }
                        }
                    }
                sw.Restart();
                TerrainLiquidTile tile = TerrainLiquidMesh.Build(root.Liquids, rf.X, rf.Y, WorldToUnity.TileOrigin(rf.X, rf.Y), null);
                buildMs.Add(sw.Elapsed.TotalMilliseconds);
                quadsAll += tile.Quads;
                foreach (LiquidSurface s in tile.Surfaces)
                    maxUvExtent = Math.Max(maxUvExtent, MaxTriangleUvExtent(s));
                if (tile.Quads != quads || tile.Vertices != vertices || tile.Surfaces.Any(s => !(MinCrossY(s) > 0) || s.Positions.Any(v => float.IsNaN(v) || float.IsInfinity(v))))
                {
                    meshProblems++;
                    say(string.Format("    {0}_{1}: mesh quads {2} vertices {3}, expected {4} and {5}", rf.X, rf.Y, tile.Quads, tile.Vertices, quads, vertices));
                }
            }
            Verdict(pinProblems == 0, client + ": " + roots.Count + " corpus roots have their pinned bytes");
            int want;
            WindowInstances.TryGetValue(client, out want);
            Verdict(instances == want && malformed == 0 && issues == 0,
                    client + ": every MH2O instance of the window decodes with no issue, " + want + " instances as recorded",
                    string.Format("{0} roots with MH2O, {1} instances, {2} malformed, {3} MH2O issues", withMh2o, instances, malformed, issues));
            Verdict(decidable == decidableOk, client + ": format by size = the rule's format wherever the instance decides it (second word 0-2, type 2)",
                    string.Format("{0} of {1} decidable; {2} decided by the material (sizes all valid); by size: {3}", decidableOk, decidable, undecided,
                                  string.Join(", ", bySize.Select(kv => (kv.Key < 0 ? "none" : LiquidFormat.BytesPerVertex(kv.Key) + " B") + " " + kv.Value))));
            Verdict(outside == 0, client + ": every height an existing quad uses lies in its instance's [min, max]", usedHeights + " used vertices, " + outside + " outside");
            Verdict(meshProblems == 0, client + ": each tile's liquid mesh = the existing quads and the vertices they use, every triangle up, finite", quadsAll + " quads");
            // Without a catalogue every stored u/v is uv0, and planar uv0 spans 1/8 per quad: the largest extent inside a triangle is
            // the largest stored step a triangle joins (taken the short way round the u16 wrap) x 1/256, or 1/8.
            double wantExtent = Math.Max(maxQuadStep * (double)LiquidFormat.StoredUvScale, 1.0 / 8);
            Verdict(Math.Abs(maxUvExtent - wantExtent) < 1e-3, client + ": uv0's largest extent inside a triangle = the largest stored u/v step a triangle joins x 1/256 " +
                    "(the u16 wrap joined), or the planar 1/8",
                    string.Format(CultureInfo.InvariantCulture, "extent {0:0.####}; largest step {1} at {2}; {3} instance(s) with stored u/v, {4} crossing the u16 wrap; " +
                                  "largest distance from an instance's reference vertex {5} (the join needs below 32,768)",
                                  maxUvExtent, maxQuadStep, maxQuadStepAt, uvInstances, uvWraps, maxRefStep));
            Verdict(maxRefStep < 16384, client + ": stored u/v lie well inside half the u16 range of their instance's reference vertex (the wrap join's premise)",
                    maxRefStep.ToString(CultureInfo.InvariantCulture));
            if (parseMs.Count > 0)
                say(string.Format(CultureInfo.InvariantCulture, "    timing {0}: root parse median {1:0.00} ms, max {2:0.00}; liquid mesh median {3:0.00} ms, max {4:0.00} ({5} tiles with MH2O)",
                                  client, Median(parseMs), parseMs.Max(), buildMs.Count > 0 ? Median(buildMs) : 0, buildMs.Count > 0 ? buildMs.Max() : 0, buildMs.Count));
        }

        /// <summary>Whether the stored u or v of an instance's used vertices crosses the u16 wrap: two used values more
        /// than 32,767 apart as stored (their nearest distance mod 65,536 is then the short way round).</summary>
        static bool StoredUvWraps(LiquidInstance li)
        {
            int minU = int.MaxValue, maxU = int.MinValue, minV = int.MaxValue, maxV = int.MinValue;
            for (int i = 0; i <= li.Height; i++)
                for (int j = 0; j <= li.Width; j++)
                {
                    if (!Used(li, i, j))
                        continue;
                    int k = i * (li.Width + 1) + j;
                    minU = Math.Min(minU, li.U[k]); maxU = Math.Max(maxU, li.U[k]);
                    minV = Math.Min(minV, li.V[k]); maxV = Math.Max(maxV, li.V[k]);
                }
            return maxU - minU > 32767 || maxV - minV > 32767;
        }

        /// <summary>The largest stored u or v step between two corners of one existing quad that a triangle joins (all pairs
        /// but b-cc: the triangles are (a, b, d) and (a, d, cc)), as a signed 16-bit difference (the short way round the wrap).</summary>
        static int MaxQuadStep(LiquidInstance li)
        {
            int max = 0, w = li.Width;
            for (int r = 0; r < li.Height; r++)
                for (int c = 0; c < w; c++)
                {
                    if (!li.QuadExists(r, c))
                        continue;
                    int[] k = { r * (w + 1) + c, r * (w + 1) + c + 1, (r + 1) * (w + 1) + c, (r + 1) * (w + 1) + c + 1 };
                    for (int a = 0; a < 4; a++)
                        for (int b = a + 1; b < 4; b++)
                            if (a != 1 || b != 2)
                                max = Math.Max(max, Math.Max(Math.Abs((int)unchecked((short)(li.U[k[a]] - li.U[k[b]]))),
                                                             Math.Abs((int)unchecked((short)(li.V[k[a]] - li.V[k[b]])))));
                }
            return max;
        }

        /// <summary>The largest stored u or v distance, as a signed 16-bit difference, from the instance's reference vertex (the
        /// first corner of its first existing quad, as TerrainLiquidMesh takes it) to any vertex an existing quad uses.</summary>
        static int MaxReferenceStep(LiquidInstance li)
        {
            int first = Array.IndexOf(li.Exists, true);
            if (first < 0)
                return 0;
            int w = li.Width, refK = (first / w) * (w + 1) + first % w, max = 0;
            for (int i = 0; i <= li.Height; i++)
                for (int j = 0; j <= w; j++)
                {
                    if (!Used(li, i, j))
                        continue;
                    int k = i * (w + 1) + j;
                    max = Math.Max(max, Math.Max(Math.Abs((int)unchecked((short)(li.U[k] - li.U[refK]))), Math.Abs((int)unchecked((short)(li.V[k] - li.V[refK])))));
                }
            return max;
        }

        /// <summary>The largest uv0 extent (max |du|, |dv|) inside one triangle of the surface.</summary>
        static double MaxTriangleUvExtent(LiquidSurface s)
        {
            double max = 0;
            for (int t = 0; t < s.Indices.Length; t += 3)
                for (int a = 0; a < 3; a++)
                    for (int b = a + 1; b < 3; b++)
                        for (int c = 0; c < 2; c++)
                            max = Math.Max(max, Math.Abs(s.Uv0[2 * s.Indices[t + a] + c] - s.Uv0[2 * s.Indices[t + b] + c]));
            return max;
        }

        static double Median(List<double> v)
        {
            var s = v.OrderBy(x => x).ToList();
            return s.Count % 2 == 1 ? s[s.Count / 2] : (s[s.Count / 2 - 1] + s[s.Count / 2]) / 2;
        }

        // ================================================================================== (b) exit tiles

        sealed class Exit
        {
            public string Client, Name;
            public int X, Y;
            public uint Root;
            public int Instances, Type, Word, Bitmaps, Attributes;
            public float Height;
            public int[] Formats;                    // counts of FormatBySize -1, 0, 1, 2, 3
            public int Quads, Vertices;              // the mesh, pinned
            public int KnownChunk;                   // a chunk checked against the terrain's grid
        }

        static readonly Exit[] Exits =
        {
            new Exit { Client = "wow", Name = "Retail 29_48 (Stormwind Harbor)", X = 29, Y = 48, Root = 777427, Instances = 229, Type = 2, Word = 42, Bitmaps = 24,
                       Attributes = -1, Height = 0f, Formats = new[] { 93, 0, 0, 136, 0 }, Quads = 13896, Vertices = 17718, KnownChunk = -1 },
            new Exit { Client = "wow_classic_era", Name = "Era 36_49 (Lake Everstill)", X = 36, Y = 49, Root = 778837, Instances = 132, Type = 5, Word = 5987, Bitmaps = 50,
                       Attributes = 132, Height = 56.1615f, Formats = new[] { 0, 132, 0, 0, 0 }, Quads = 6958, Vertices = 9030, KnownChunk = 72 },
        };

        static void ExitTiles(string corpus, TerrainCorpusTests.Pins pins)
        {
            foreach (Exit e in Exits)
            {
                say("-- liquids, exit tile " + e.Name);
                string problem;
                byte[] bytes = ReadPinned(corpus, pins, "corpus/" + e.Client + "/azeroth/" + e.Root + ".bin", out problem);
                Verdict(bytes != null, e.Name + ": root " + e.Root + " has its pinned bytes", problem);
                if (bytes == null)
                    continue;
                var parseMs = new List<double>();
                var buildMs = new List<double>();
                AdtRoot root = null;
                TerrainLiquidTile tile = null;
                WorldOrigin origin = WorldToUnity.TileOrigin(e.X, e.Y);
                for (int run = 0; run < 5; run++)
                {
                    var sw = Stopwatch.StartNew();
                    root = AdtParser.ParseRoot(bytes, e.X, e.Y, e.Name);
                    parseMs.Add(sw.Elapsed.TotalMilliseconds);
                    sw.Restart();
                    tile = TerrainLiquidMesh.Build(root.Liquids, e.X, e.Y, origin, null);
                    buildMs.Add(sw.Elapsed.TotalMilliseconds);
                }
                AdtLiquids l = root.Liquids;
                List<LiquidInstance> all = l.Instances.SelectMany(x => x).ToList();
                var formats = new int[5];
                foreach (LiquidInstance li in all)
                    formats[li.FormatBySize + 1]++;
                bool flat = all.All(li => li.MinHeight == li.MaxHeight && Math.Abs(li.MinHeight - e.Height) < 1e-4 && li.MinHeight == all[0].MinHeight &&
                                          (li.Heights == null || Enumerable.Range(0, li.VertexCount).All(k => !Used(li, k / (li.Width + 1), k % (li.Width + 1)) || li.Heights[k] == li.MinHeight)));
                int attributes = l.Attributes.Count(a => a.Present);
                Verdict(all.Count == e.Instances && l.Instances.Count(x => x.Length > 0) == e.Instances && all.All(li => li.Status == WorldDataStatus.Decoded) &&
                        all.All(li => li.LiquidType == e.Type && li.ObjectOrFormat == e.Word) && flat && formats.SequenceEqual(e.Formats) &&
                        all.Count(li => li.HasExistsBitmap) == e.Bitmaps && (e.Attributes < 0 || attributes == e.Attributes),
                        string.Format("{0}: {1} chunks with one instance each, all type {2} word {3}, flat at {4}, formats by size {5}, {6} bitmaps",
                                      e.Name, e.Instances, e.Type, e.Word, F(e.Height), string.Join("/", e.Formats), e.Bitmaps),
                        string.Format("{0} instances, formats none/0/1/2/3 {1}, {2} bitmaps, {3} attribute blocks, height {4:R}",
                                      all.Count, string.Join("/", formats), all.Count(li => li.HasExistsBitmap), attributes, all.Count > 0 ? all[0].MinHeight : float.NaN));
                int quads = all.Sum(li => li.Exists.Count(x => x));
                int used = all.Sum(li => Enumerable.Range(0, li.VertexCount).Count(k => Used(li, k / (li.Width + 1), k % (li.Width + 1))));
                LiquidSurface s = tile.Surfaces.Count == 1 ? tile.Surfaces[0] : null;
                Verdict(s != null && s.LiquidType == e.Type && tile.Quads == quads && tile.Vertices == used && tile.Quads == e.Quads && tile.Vertices == e.Vertices &&
                        s.Instances == e.Instances && MinCrossY(s) > 0 && tile.MissingTypes == 1 && tile.FormatDisagreements == 0,
                        string.Format("{0}: one surface, {1} quads and {2} vertices (= the bitmaps' quads and the vertices they use), every triangle up", e.Name, e.Quads, e.Vertices),
                        string.Format("{0} quads, {1} vertices, {2} surface(s), {3} geometry bytes", tile.Quads, tile.Vertices, tile.Surfaces.Count, tile.GeometryBytes));
                say(string.Format(CultureInfo.InvariantCulture, "    timing {0}: parse ms {1}; liquid mesh ms {2}", e.Name,
                                  string.Join(" ", parseMs.Select(v => v.ToString("0.00", CultureInfo.InvariantCulture))),
                                  string.Join(" ", buildMs.Select(v => v.ToString("0.00", CultureInfo.InvariantCulture)))));

                // A known chunk: every liquid vertex bit-exactly over the terrain's outer grid point (row y + i, column x + j).
                int chunk = e.KnownChunk >= 0 ? e.KnownChunk : Array.FindIndex(l.Instances, x => x.Length > 0);
                TerrainTileMesh terrain = TerrainMeshBuilder.Build(root, e.X, e.Y, origin);
                var only = new AdtLiquids { Status = l.Status, Headers = l.Headers, Attributes = l.Attributes, Instances = new LiquidInstance[256][] };
                for (int n = 0; n < 256; n++)
                    only.Instances[n] = n == chunk ? l.Instances[n] : new LiquidInstance[0];
                LiquidSurface cs = TerrainLiquidMesh.Build(only, e.X, e.Y, origin, null).Surfaces[0];
                LiquidInstance ci = l.Instances[chunk][0];
                var grid = new Dictionary<long, int>();
                float[] tp = terrain.Chunks[chunk].Positions;
                for (int r = 0; r <= 8; r++)
                    for (int c = 0; c <= 8; c++)
                    {
                        int k = TerrainVertexLayout.Outer(r, c);
                        grid[Key(tp[3 * k], tp[3 * k + 2])] = r * 9 + c;
                    }
                int onGrid = 0;
                for (int v = 0; v < cs.VertexCount; v++)
                {
                    int rc;
                    if (grid.TryGetValue(Key(cs.Positions[3 * v], cs.Positions[3 * v + 2]), out rc) &&
                        cs.Positions[3 * v + 1] == ci.HeightAt(rc / 9 - ci.Y, rc % 9 - ci.X))
                        onGrid++;
                }
                Verdict(cs.VertexCount > 0 && onGrid == cs.VertexCount,
                        string.Format("{0}: chunk {1}'s {2} liquid vertices lie bit-exactly on the terrain's outer grid points, at the instance's height", e.Name, chunk, cs.VertexCount),
                        onGrid + " of " + cs.VertexCount);

                // Seams: points where two chunks' liquid meet have bit-equal x and z and equal heights.
                var seen = new Dictionary<long, float>();
                int shared = 0, equal = 0;
                foreach (LiquidSurface ss in tile.Surfaces)
                    for (int v = 0; v < ss.VertexCount; v++)
                    {
                        long k = Key(ss.Positions[3 * v], ss.Positions[3 * v + 2]);
                        float y;
                        if (seen.TryGetValue(k, out y))
                        {
                            shared++;
                            if (y == ss.Positions[3 * v + 1]) equal++;
                        }
                        else
                            seen[k] = ss.Positions[3 * v + 1];
                    }
                Verdict(shared > 0 && shared == equal, e.Name + ": every point where neighbouring chunks' liquid meets is bit-equal in x, z and height",
                        shared + " shared points, " + equal + " equal");

                if (e.Client == "wow_classic_era")
                    EraWalkthrough(bytes, l);
            }
        }

        /// <summary>Era 36_49 chunk 72 (the investigation's byte walkthrough): the MH2O chunk header at file 84 (payload 92),
        /// the instance at file 3164, attributes at 6332, the bitmap 00 00 80 c0 e0 e0 e0 f0 at 6348, vertex data at 6356.</summary>
        static void EraWalkthrough(byte[] bytes, AdtLiquids l)
        {
            byte[] record = { 0x05, 0x00, 0x63, 0x17, 0x58, 0xa5, 0x60, 0x42, 0x58, 0xa5, 0x60, 0x42, 0x00, 0x00, 0x08, 0x08, 0x70, 0x18, 0x00, 0x00, 0x78, 0x18, 0x00, 0x00 };
            byte[] bitmap = { 0x00, 0x00, 0x80, 0xc0, 0xe0, 0xe0, 0xe0, 0xf0 };
            LiquidInstance li = l.Instances[72][0];
            bool bytesOk = bytes.Skip(3164).Take(24).SequenceEqual(record) && bytes.Skip(6348).Take(8).SequenceEqual(bitmap);
            bool bits = li.Exists.Length == 64;
            for (int q = 0; q < 64 && bits; q++)
                bits = li.Exists[q] == (((bitmap[q >> 3] >> (q & 7)) & 1) != 0);
            Verdict(bytesOk && l.DataOffset == 92 && l.Headers[72].InstancesOffset == 3072 && l.Headers[72].AttributesOffset == 6332 - 92 &&
                    l.DataOffset + li.ExistsOffset == 6348 && l.DataOffset + li.VertexOffset == 6356 && li.LiquidType == 5 && li.ObjectOrFormat == 5987 &&
                    li.MinHeight == BitConverter.ToSingle(record, 4) && li.Width == 8 && li.Height == 8 && bits && li.Exists.Count(x => x) == 16 &&
                    li.FormatBySize == 0 && li.VertexBlockBytes == 405 && l.Attributes[72].Present,
                    "Era 36_49 chunk 72 walkthrough: record at file 3164, attributes 6332, bitmap 6348 (16 quads, bit r*8 + c LSB first), 405-byte format-0 block at 6356",
                    string.Format("payload {0}, exists +{1}, vertices +{2}, {3} quads", l.DataOffset, li.ExistsOffset, li.VertexOffset, li.Exists.Count(x => x)));
        }

        static long Key(float x, float z)
        {
            return ((long)BitConverter.SingleToInt32Bits(x) << 32) | (uint)BitConverter.SingleToInt32Bits(z);
        }

        // ================================================================================== (c) MLIQ examples

        /// <summary>One example group, its figures from the investigation's census (mliq-bytes/mliq_groups.json, an
        /// independent reader): counts, corner and used-height range as float32, size, present tiles, nibble, group
        /// liquid, MOHD and MOGP flags; Pattern 'd' (depth byte, bytes 1-3 zero) or 's' (i16 s, t) by the census's
        /// liquid kind; Type = the contract's group-type rule worked by hand; Lvf for the test catalogue.</summary>
        sealed class Group
        {
            public string Client;
            public uint Root, Fid;
            public int X, Y, A, B, Material, Size, Present, Nibble, Type, Lvf;
            public double Cx, Cy, Cz, Hmin, Hmax;
            public uint GroupLiquid, GroupFlags;
            public ushort RootFlags;
            public char Pattern;

            public Group(string client, uint root, uint fid, int x, int y, int a, int b, double cx, double cy, double cz, int material, int size, int present,
                         int nibble, double hmin, double hmax, uint gl, ushort mohd, uint flags, char pattern, int type, int lvf)
            {
                Client = client; Root = root; Fid = fid; X = x; Y = y; A = a; B = b; Cx = cx; Cy = cy; Cz = cz; Material = material; Size = size;
                Present = present; Nibble = nibble; Hmin = hmin; Hmax = hmax; GroupLiquid = gl; RootFlags = mohd; GroupFlags = flags; Pattern = pattern;
                Type = type; Lvf = lvf;
            }
        }

        static readonly Group[] Groups =
        {
            new Group("wow", 108104, 108105, 4, 5, 3, 4, -29.16666603088379, -8.333333015441895, 1.46337890625, 10, 202, 10, 0, 1.46337890625, 1.46337890625, 5, 0x0005, 0x00001009, 'd', 13, 0),
            new Group("wow", 108238, 108279, 55, 82, 54, 81, -154.16665649414062, -129.16665649414062, -68.60888671875, 13, 40484, 3232, 0, -68.60888671875, -60.8991584777832, 7, 0x000F, 0x03003805, 's', 19, 1),
            new Group("wow", 108238, 108284, 30, 32, 29, 31, -441.6666564941406, -120.83332824707031, -97.6294174194336, 13, 8609, 680, 0, -97.6294174194336, -97.6294174194336, 7, 0x000F, 0x00003805, 's', 19, 1),
            new Group("wow", 109280, 109369, 17, 10, 16, 9, -16.66666603088379, 408.33331298828125, -59.0416145324707, 31, 1534, 144, 0, -59.0416145324707, -59.0416145324707, 1, 0x0005, 0x00003805, 'd', 13, 0),
            new Group("wow", 110060, 110122, 6, 5, 5, 4, -408.33331298828125, 362.5, 0.1129625141620636, 65, 290, 20, 7, 0.1129625141620636, 0.1129625141620636, 15, 0x0000, 0x00003805, 's', 20, 1),
            new Group("wow", 111185, 111264, 5, 6, 4, 5, -108.33332824707031, -41.666664123535156, -5.65242338180542, 15, 290, 18, 0, -5.65242338180542, -5.236077308654785, 1, 0x0005, 0x00003805, 'd', 13, 0),
            new Group("wow", 111185, 111265, 5, 6, 4, 5, -91.66666412353516, 12.5, -5.1465559005737305, 15, 290, 19, 0, -5.1465559005737305, -4.995521545410156, 1, 0x0005, 0x00003805, 'd', 13, 0),
            new Group("wow", 111185, 111266, 13, 9, 12, 8, -162.5, -75.0, -20.056396484375, 15, 1062, 72, 0, -20.056396484375, -20.056396484375, 1, 0x0005, 0x00003805, 'd', 13, 0),
            new Group("wow", 111185, 111269, 8, 11, 7, 10, -191.66665649414062, -16.66666603088379, -6.009783744812012, 15, 804, 41, 0, -6.009783744812012, -6.009783744812012, 1, 0x0005, 0x00003805, 'd', 13, 0),
            new Group("wow", 4235933, 4274647, 62, 62, 61, 61, -150.0, -204.16665649414062, -175.79383850097656, 103, 34503, 3721, 0, -175.79383850097656, -175.79383850097656, 1177, 0x041F, 0x83003A05, 's', 1177, 3),
            new Group("wow_classic_beta", 7962574, 7964807, 6, 6, 5, 5, -499.9999694824219, -345.83331298828125, -7.636057376861572, 166, 343, 25, 0, -7.636057376861572, -7.636057376861572, 5, 0x041F, 0x00003A41, 'd', 13, 0),
            new Group("wow_classic_beta", 7962574, 7964878, 98, 143, 97, 142, -474.9999694824219, -304.1666564941406, -6.482954502105713, 167, 125916, 2677, 0, -6.482954502105713, -6.48294734954834, 1344, 0x041F, 0x80201A0D, 'd', 1344, 0),
            new Group("wow_classic_era", 107243, 107340, 16, 10, 15, 9, -470.83331298828125, 133.3333282470703, -6.482952117919922, 115, 1445, 109, 4, -6.482952117919922, -6.482951641082764, 15, 0x0000, 0x0000BA41, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107341, 16, 16, 15, 15, -358.33331298828125, 108.33332824707031, -6.482946872711182, 115, 2303, 165, 4, -6.482946872711182, -6.482946872711182, 15, 0x0000, 0x0000BA41, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107342, 13, 13, 12, 12, -316.6666564941406, 133.3333282470703, -6.482946872711182, 115, 1526, 98, 4, -6.482946872711182, -6.482946872711182, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107343, 12, 9, 11, 8, -316.6666564941406, 83.33332824707031, -6.482946872711182, 115, 982, 52, 4, -6.482946872711182, -6.482946872711182, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107344, 12, 11, 11, 10, -249.99998474121094, 75.0, -6.48294734954834, 115, 1196, 65, 4, -6.48294734954834, -6.48294734954834, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107345, 12, 10, 11, 9, -254.16665649414062, 133.3333282470703, -6.48294734954834, 115, 1089, 58, 4, -6.48294734954834, -6.48294734954834, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107346, 11, 10, 10, 9, -112.49999237060547, 112.49999237060547, -6.482954978942871, 115, 1000, 87, 4, -6.482954978942871, -6.482954978942871, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107347, 28, 14, 27, 13, -220.8333282470703, 95.83332824707031, -6.482946872711182, 115, 3517, 291, 4, -6.482946872711182, -6.482946872711182, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107348, 20, 9, 19, 8, -466.6666564941406, -150.0, -6.48294734954834, 115, 1622, 140, 4, -6.48294734954834, -6.48294734954834, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107349, 14, 32, 13, 31, -287.5, 158.3333282470703, -6.48294734954834, 115, 4017, 294, 4, -6.48294734954834, -6.48294734954834, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107350, 23, 11, 22, 10, -200.0, -133.3333282470703, -6.48294734954834, 115, 2274, 175, 4, -6.48294734954834, -6.48294734954834, 15, 0x0000, 0x0000BA41, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107351, 15, 11, 14, 10, -412.4999694824219, 129.16665649414062, -6.482946872711182, 115, 1490, 135, 4, -6.482946872711182, -6.482946872711182, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107352, 14, 11, 13, 10, -291.6666564941406, 50.0, -6.482946872711182, 115, 1392, 95, 4, -6.482946872711182, -6.482946872711182, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107400, 19, 19, 18, 18, -325.0, -100.0, -6.48294734954834, 115, 3242, 185, 4, -6.48294734954834, -6.48294734954834, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107401, 19, 15, 18, 14, -295.83331298828125, -124.99999237060547, -6.48294734954834, 115, 2562, 159, 4, -6.48294734954834, -6.48294734954834, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107402, 11, 17, 10, 16, -350.0, -133.3333282470703, -6.482946872711182, 115, 1686, 75, 4, -6.482946872711182, -6.482946872711182, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107403, 13, 12, 12, 11, -391.6666564941406, -158.3333282470703, -6.482946872711182, 115, 1410, 102, 4, -6.482946872711182, -6.482946872711182, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107405, 11, 11, 10, 10, -358.33331298828125, -183.3333282470703, -6.482946872711182, 115, 1098, 68, 4, -6.482946872711182, -6.482946872711182, 15, 0x0000, 0x0000BA41, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107406, 9, 32, 8, 31, -341.6666564941406, -304.1666564941406, -6.48294734954834, 115, 2582, 229, 4, -6.48294734954834, -6.48294734954834, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107407, 16, 12, 15, 11, -258.33331298828125, -133.3333282470703, -6.48294734954834, 115, 1731, 101, 4, -6.48294734954834, -6.48294734954834, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107408, 11, 21, 10, 20, -287.5, -29.16666603088379, -6.48294734954834, 115, 2078, 167, 4, -6.48294734954834, -6.48294734954834, 15, 0x0000, 0x0000B841, 'd', 13, 0),
            new Group("wow_classic_era", 107243, 107496, 6, 6, 5, 5, -499.9999694824219, -345.83331298828125, -7.636057376861572, 115, 343, 25, 4, -7.636057376861572, -7.636057376861572, 15, 0x0000, 0x0000BA41, 'd', 13, 0),
            new Group("wow_classic_era", 108238, 108279, 55, 82, 54, 81, -154.16665649414062, -129.16665649414062, -68.60888671875, 4, 40484, 3232, 6, -68.60888671875, -60.8991584777832, 15, 0x0000, 0x00003A05, 's', 19, 1),
            new Group("wow_classic_era", 108238, 108284, 30, 32, 29, 31, -441.6666564941406, -120.83332824707031, -97.6294174194336, 4, 8609, 680, 6, -97.6294174194336, -97.6294174194336, 15, 0x0000, 0x00003805, 's', 19, 1),
            new Group("wow_classic_era", 109280, 109307, 17, 10, 16, 9, -50.0, -445.83331298828125, -59.04161834716797, 25, 1534, 144, 0, -59.04161834716797, -59.04161834716797, 15, 0x0000, 0x00003805, 'd', 13, 0),
            new Group("wow_classic_era", 111185, 111264, 5, 6, 4, 5, -108.33332824707031, -41.666664123535156, -5.65242338180542, 16, 290, 18, 0, -5.65242338180542, -5.236077308654785, 15, 0x0000, 0x00003A05, 'd', 13, 0),
            new Group("wow_classic_era", 111185, 111265, 5, 6, 4, 5, -91.66666412353516, 12.5, -5.1465559005737305, 16, 290, 19, 0, -5.1465559005737305, -4.995521545410156, 15, 0x0000, 0x00003A05, 'd', 13, 0),
            new Group("wow_classic_era", 111185, 111266, 13, 9, 12, 8, -162.5, -75.0, -20.056396484375, 16, 1062, 72, 0, -20.056396484375, -20.056396484375, 15, 0x0000, 0x00003A05, 'd', 13, 0),
            new Group("wow_classic_era", 111185, 111269, 8, 11, 7, 10, -191.66665649414062, -16.66666603088379, -6.009783744812012, 16, 804, 41, 0, -6.009783744812012, -6.009783744812012, 15, 0x0000, 0x00003A05, 'd', 13, 0),
        };

        static void MliqExamples(string folder)
        {
            say("-- liquids, MLIQ examples in " + folder);
            string manifestPath = Path.Combine(folder, "manifest.json");
            string manifestSha = File.Exists(manifestPath) ? TerrainCorpusTests.Sha256(manifestPath) : "missing";
            Verdict(manifestSha == ExamplesManifestSha256, "MLIQ examples: manifest.json has its pinned SHA-256", manifestSha.Substring(0, Math.Min(16, manifestSha.Length)));
            if (manifestSha != ExamplesManifestSha256)
                return;
            var shas = new Dictionary<string, string>(StringComparer.Ordinal);
            using (JsonDocument doc = JsonDocument.Parse(File.ReadAllText(manifestPath)))
                foreach (JsonProperty c in doc.RootElement.GetProperty("files").EnumerateObject())
                    foreach (JsonProperty f in c.Value.EnumerateObject())
                        shas[c.Name + "/" + f.Name] = f.Value.GetProperty("sha256").GetString();
            var roots = new Dictionary<string, WmoRoot>(StringComparer.Ordinal);
            int changed = 0;
            Func<string, uint, byte[]> read = (client, fid) =>
            {
                string path = Path.Combine(folder, client, fid + ".bin"), want;
                if (!File.Exists(path) || !shas.TryGetValue(client + "/" + fid, out want) || TerrainCorpusTests.Sha256(path) != want)
                {
                    changed++;
                    say("    " + client + "/" + fid + ".bin is missing or not the manifest's bytes");
                    return null;
                }
                return File.ReadAllBytes(path);
            };
            int maxStep = 0;
            double extentD = 0, extentS = 0;
            foreach (Group g in Groups)
            {
                string tag = g.Client + " " + g.Fid;
                WmoRoot root;
                if (!roots.TryGetValue(g.Client + "/" + g.Root, out root))
                {
                    byte[] rb = read(g.Client, g.Root);
                    root = rb != null ? WmoParser.ParseRoot(rb, g.Client + " root " + g.Root) : null;
                    roots[g.Client + "/" + g.Root] = root;
                }
                byte[] gb = read(g.Client, g.Fid);
                if (root == null || gb == null)
                {
                    Verdict(false, tag + ": the example's root and group files are the manifest's");
                    continue;
                }
                WmoGroup group = WmoParser.ParseGroup(gb, tag);
                string problem;
                WmoLiquid l = WmoLiquid.FromGroup(group, out problem);
                WmoChunkInfo chunk;
                group.TryGetChunk("MLIQ", out chunk);
                if (l == null)
                {
                    Verdict(false, tag + ": MLIQ parses", problem);
                    continue;
                }
                float hmin = float.PositiveInfinity, hmax = float.NegativeInfinity;
                int used = 0, nonZeroHigh = 0;
                for (int j = 0; j < l.VerticesY; j++)
                    for (int i = 0; i < l.VerticesX; i++)
                    {
                        if (!l.VertexUsed(i, j))
                            continue;
                        used++;
                        float h = l.Height(i, j);
                        hmin = Math.Min(hmin, h);
                        hmax = Math.Max(hmax, h);
                        if ((l.VertexWords[l.VertexIndex(i, j)] >> 8) != 0)
                            nonZeroHigh++;
                        if (g.Pattern == 's')
                        {
                            if (i > 0 && l.VertexUsed(i - 1, j))
                                maxStep = Math.Max(maxStep, Math.Max(Math.Abs(l.S(i, j) - l.S(i - 1, j)), Math.Abs(l.T(i, j) - l.T(i - 1, j))));
                            if (j > 0 && l.VertexUsed(i, j - 1))
                                maxStep = Math.Max(maxStep, Math.Max(Math.Abs(l.S(i, j) - l.S(i, j - 1)), Math.Abs(l.T(i, j) - l.T(i, j - 1))));
                        }
                    }
                bool header = l.VerticesX == g.X && l.VerticesY == g.Y && l.TilesX == g.A && l.TilesY == g.B && l.Corner.X == (float)g.Cx &&
                              l.Corner.Y == (float)g.Cy && l.Corner.Z == (float)g.Cz && l.Material == g.Material && chunk.Size == g.Size &&
                              chunk.Size == WmoLiquid.HeaderSize + 8 * g.X * g.Y + g.A * g.B;
                bool tiles = l.PresentTiles == g.Present && l.Nibble == g.Nibble && !l.MixedNibbles && hmin == (float)g.Hmin && hmax == (float)g.Hmax;
                bool pattern = g.Pattern == 'd' ? nonZeroHigh == 0 : nonZeroHigh > 0;
                int type = WmoLiquid.GroupLiquidType(root.Header.Flags, group.Header.GroupLiquid, group.Header.Flags, l.Nibble);
                bool flags = root.Header.Flags == g.RootFlags && group.Header.Flags == g.GroupFlags && group.Header.GroupLiquid == g.GroupLiquid;
                var catalog = new LiquidCatalog();
                catalog.Add(new LiquidTypeView { Id = type, Lvf = g.Lvf, Kind = g.Pattern == 'd' ? (int)LiquidKind.Water : (int)LiquidKind.Magma, Float0 = 1f });
                LiquidSurface s = WmoLiquidMesh.Build(l, type, catalog);
                if (g.Pattern == 'd') extentD = Math.Max(extentD, MaxTriangleUvExtent(s));
                else extentS = Math.Max(extentS, MaxTriangleUvExtent(s));
                bool mesh = s.QuadCount == g.Present && s.VertexCount == used && MinCrossY(s) > 0 && s.BoundsMin[1] == hmin && s.BoundsMax[1] == hmax &&
                            (g.Pattern == 's' ? s.StoredUvInstances == 1 : s.PlanarUvInstances == 1) &&
                            s.Uv0.All(v => !float.IsNaN(v) && !float.IsInfinity(v));
                Verdict(header && tiles && pattern && flags && type == g.Type && mesh,
                        string.Format("{0}: X {1} Y {2} A {3} B {4}, corner ({5}, {6}, {7}), material {8}, {9} bytes; {10} present tiles, nibble {11}, heights {12}..{13}; " +
                                      "{14} payload; group type {15}; mesh {10} quads, every triangle up",
                                      tag, g.X, g.Y, g.A, g.B, F(g.Cx), F(g.Cy), F(g.Cz), g.Material, g.Size, g.Present, g.Nibble, F(g.Hmin), F(g.Hmax),
                                      g.Pattern == 'd' ? "depth-byte" : "s/t", g.Type),
                        string.Format("header {0}, tiles {1}, pattern {2} ({3} used vertices with bytes 1-3 set), flags {4}, type {5}, mesh {6} ({7} quads, {8} vertices)",
                                      header, tiles, pattern, nonZeroHigh, flags, type, mesh, s.QuadCount, s.VertexCount));
            }
            Verdict(changed == 0, "MLIQ examples: every file read has the manifest's SHA-256");
            Verdict(extentD < 1.0, "MLIQ examples, depth reading: planar uv0 spans less than one repeat inside every triangle",
                    extentD.ToString("0.####", CultureInfo.InvariantCulture));
            say(string.Format(CultureInfo.InvariantCulture, "    s/t groups: largest i16 step between neighbouring used vertices {0}; largest uv0 extent in a triangle {1:0.####} " +
                              "(raw s/t x 1/256, not unwrapped: no step comes near the i16 wrap)", maxStep, extentS));
        }
    }
}
