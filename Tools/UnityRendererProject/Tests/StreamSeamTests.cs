// StreamSeamTests.cs
//
// Stage 08 (docs/world-viewer/08-streaming.md 1.8, seams): every pair of neighbouring tiles of the Stage 08 region --
// Azeroth 30..32 x 47..49, the census's (Tools/world-audit/world08_corpus.py) -- on the four clients, built as a streaming
// session builds them, against ONE origin (the entry tile 31_48's north-west corner), and their shared edge compared vertex by
// vertex. Two tiles' edge vertices meet where their positions' x and z are bit-equal (the same integer-index expression in
// double, one origin, one float cast); their heights come from each tile's own MCNK base and MCVT and are compared as built:
// how many bit-equal, the largest difference. A seam is closed when every edge vertex of one tile has its partner on the other
// at the same x and z, at a height that differs by no more than the tolerance (the audit's 5e-5 yd, 00-audit.md).

using System;
using System.Collections.Generic;
using System.IO;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class StreamSeamTests
    {
        const uint AzerothWdt = 775971;
        const double HeightTolerance = 5e-5;

        static int failures;
        static readonly List<string> log = new List<string>();

        static void Check(bool condition, string name)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name); failures++; }
        }

        static byte[] Read(string dir, string alt, uint fid)
        {
            foreach (string d in new[] { dir, alt })
            {
                string p = Path.Combine(d, fid + ".bin");
                if (File.Exists(p))
                    return File.ReadAllBytes(p);
            }
            return null;
        }

        /// <summary>The edge vertices of a mesh where one coordinate (axis 0: Unity x, 2: Unity z) is at its extreme (max or
        /// min), keyed by the other horizontal coordinate's bits.</summary>
        static Dictionary<int, float[]> Edge(TerrainTileMesh m, int axis, bool max, out float at)
        {
            at = max ? float.NegativeInfinity : float.PositiveInfinity;
            foreach (TerrainChunkMesh c in m.Chunks)
                if (c != null && c.Positions != null)
                    for (int k = 0; k + 2 < c.Positions.Length; k += 3)
                    {
                        float v = c.Positions[k + axis];
                        if (max ? v > at : v < at) at = v;
                    }
            var edge = new Dictionary<int, float[]>();
            int other = axis == 0 ? 2 : 0;
            foreach (TerrainChunkMesh c in m.Chunks)
                if (c != null && c.Positions != null)
                    for (int k = 0; k + 2 < c.Positions.Length; k += 3)
                        if (c.Positions[k + axis] == at)
                            edge[BitConverter.ToInt32(BitConverter.GetBytes(c.Positions[k + other]), 0)] =
                                new[] { c.Positions[k], c.Positions[k + 1], c.Positions[k + 2] };
            return edge;
        }

        public static int Run(string corpus, Action<string> output)
        {
            failures = 0;
            log.Clear();
            WorldOrigin origin = WorldToUnity.TileOrigin(31, 48);
            foreach (string client in new[] { "wow", "wow_classic_era", "wow_classic", "wow_classic_beta" })
            {
                string dir = Path.Combine(corpus, "world08", client), alt = Path.Combine(corpus, "world08", "corpus", client);
                byte[] wdtBytes = Read(dir, alt, AzerothWdt);
                if (wdtBytes == null)
                {
                    log.Add("  SKIP  " + client + ": no Azeroth WDT in " + dir);
                    continue;
                }
                WdtFile wdt = WdtParser.Parse(wdtBytes, client + " azeroth.wdt");
                var meshes = new Dictionary<int, TerrainTileMesh>();
                var missing = new List<string>();
                for (int y = 47; y <= 49; y++)
                    for (int x = 30; x <= 32; x++)
                    {
                        WdtTileFiles files;
                        byte[] root = wdt.TryGetTileFiles(x, y, out files) ? Read(dir, alt, files[WdtTileFileRole.RootAdt]) : null;
                        if (root == null)
                        {
                            missing.Add(x + "_" + y);
                            continue;
                        }
                        meshes[y * 64 + x] = TerrainMeshBuilder.Build(AdtParser.ParseRoot(root, x, y, client + " " + x + "_" + y), x, y, origin);
                    }
                int pairs = 0, vertices = 0, unpaired = 0, heightsEqual = 0, overTolerance = 0;
                double maxDh = 0;
                string worst = "";
                for (int y = 47; y <= 49; y++)
                    for (int x = 30; x <= 32; x++)
                        foreach (int[] n in new[] { new[] { x + 1, y }, new[] { x, y + 1 } })
                        {
                            TerrainTileMesh a, b;
                            if (n[0] > 32 || n[1] > 49 || !meshes.TryGetValue(y * 64 + x, out a) || !meshes.TryGetValue(n[1] * 64 + n[0], out b))
                                continue;
                            // East (tileX + 1): Unity x grows with tileX -- a's largest x is b's smallest. South (tileY + 1): Unity z
                            // falls as tileY grows -- a's smallest z is b's largest.
                            bool east = n[0] == x + 1;
                            int axis = east ? 0 : 2;
                            float atA, atB;
                            Dictionary<int, float[]> ea = Edge(a, axis, east, out atA), eb = Edge(b, axis, !east, out atB);
                            pairs++;
                            if (atA != atB)
                            {
                                unpaired += ea.Count;
                                worst = string.Format("{0}_{1}/{2}_{3}: the edges stand apart ({4:R} vs {5:R})", x, y, n[0], n[1], atA, atB);
                                continue;
                            }
                            foreach (KeyValuePair<int, float[]> kv in ea)
                            {
                                float[] pb;
                                if (!eb.TryGetValue(kv.Key, out pb))
                                {
                                    unpaired++;
                                    continue;
                                }
                                vertices++;
                                double dh = Math.Abs((double)kv.Value[1] - pb[1]);
                                if (kv.Value[1] == pb[1]) heightsEqual++;
                                if (dh > HeightTolerance) overTolerance++;
                                if (dh > maxDh)
                                {
                                    maxDh = dh;
                                    worst = string.Format("{0}_{1}/{2}_{3} at ({4:R}, {5:R}): {6:R} vs {7:R}", x, y, n[0], n[1], kv.Value[0], kv.Value[2],
                                                          kv.Value[1], pb[1]);
                                }
                            }
                            unpaired += Math.Max(0, eb.Count - ea.Count);
                        }
                Check(missing.Count == 0 && pairs == 12 && vertices == 12 * 129 && unpaired == 0 && overTolerance == 0,
                      string.Format("{0}: the 12 seams of the region, built against one origin (31_48's corner): {1} edge vertices, every one at " +
                                    "the same x and z as its neighbour's ({2} unpaired); heights bit-equal at {3} of them, the largest difference " +
                                    "{4:0.######} yd ({5} over {6} yd){7}{8}", client, vertices, unpaired, heightsEqual, maxDh, overTolerance, HeightTolerance,
                                    worst.Length > 0 ? "; largest at " + worst : "",
                                    missing.Count > 0 ? "; tiles missing from the corpus: " + string.Join(" ", missing.ToArray()) : ""));
            }
            foreach (string line in log) output(line);
            output(string.Format("StreamSeamTests: {0} failure(s)", failures));
            return failures;
        }
    }
}
