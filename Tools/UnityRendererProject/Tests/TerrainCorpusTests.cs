// TerrainCorpusTests.cs
//
// The terrain mesh builder (Assets/Scripts/Wow/World/TerrainMesh.cs) on REAL client bytes: the pinned
// tiles of the stage 03 design (section 7), read from the audit corpus (WMV_WORLD_AUDIT_CORPUS, the
// git-ignored build/world-audit-data). Runs only from the console runner (file IO, JSON, hashing).
//
//   1. Inputs: every WDT and root used is checked against Tools/world-audit/tests/corpus_hashes.json
//      (its own SHA-256 pin, or the digest of the pinned file set that holds it). A changed input stops
//      the tests: it is never accepted silently.
//   2. Each pinned root FileDataID is taken from the bytes -- MAID word 0 of the client's Azeroth WDT --
//      and compared with the design's table; the minimap id (MAID word 7) is reported for the
//      orientation test (Tools/world-audit/minimap_orientation.py).
//   3. Exact pinned counts (chunks, vertices, triangles, hole cells, hole chunks and masks), and the
//      checks that hold for real heights: cell structure, |Cross.y - U^2/2| <= 1e-3, chunk-border
//      copies bit-equal, normals against an independent recomputation, first row/column exactly 0,
//      origin-local precision, referenced-vertex bounds, the stored-position generator.
//   4. Parse and build time per tile over 5 runs (the first includes JIT).
//   5. With WMV_TERRAIN_DUMP_DIR set, each pinned tile's mesh is written there in the TerrainDump
//      format (runner_<client>_<x>_<y>.txt), for Tools/world-audit/compare_terrain_mesh.py. With
//      WMV_TERRAIN_ALL_TILES=1 every root tile of every client's corpus window is built and checked
//      too (and dumped when the dump folder is set). The dump folder also gets one synthetic root with
//      malformed MCNR/MCCV chunks and its dump (synthetic_sidechunks_32_50.bin, runner_synthetic_...),
//      so the Python reference's skip rule is compared with the builder's.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class TerrainCorpusTests
    {
        const double T = TerrainCoordinates.TileSize;
        const uint AzerothWdt = 775971;

        sealed class Case
        {
            public string Client;
            public int X, Y;
            public uint Root;
            public int HoleCells, Triangles;
            public int[] HoleChunks;                        // exactly these chunks have holes
            public Dictionary<int, ulong> Masks = new Dictionary<int, ulong>();   // pinned masks of some of them
        }

        // Stage 03 design, section 7 (the hole sets were measured on the bytes by the design review).
        static readonly Case[] Cases = BuildCases();

        static Case[] BuildCases()
        {
            var list = new List<Case>();
            foreach (string c in new[] { "wow", "wow_classic_era" })
                list.Add(new Case { Client = c, X = 32, Y = 50, Root = 778037, HoleCells = 0, Triangles = 65536, HoleChunks = new int[0] });
            list.Add(new Case
            {
                Client = "wow", X = 34, Y = 46, Root = 778417, HoleCells = 256, Triangles = 64512, HoleChunks = new[] { 3, 4, 5, 19, 20, 21, 36, 37, 145, 146 },
                Masks = { { 4, ulong.MaxValue }, { 20, ulong.MaxValue }, { 3, 0xC0C0UL }, { 21, 0x3F3F3F3F3F3F3F3FUL } },
            });
            list.Add(new Case
            {
                Client = "wow", X = 29, Y = 50, Root = 777437, HoleCells = 24, Triangles = 65440, HoleChunks = new[] { 164, 180 },
                Masks = { { 164, 0x3F3F000000000000UL }, { 180, 0x3F3FUL } },
            });
            foreach (string c in new[] { "wow", "wow_classic_era", "wow_classic", "wow_classic_beta" })
                list.Add(new Case
                {
                    Client = c, X = 32, Y = 48, Root = 778027, HoleCells = 16, Triangles = 65472, HoleChunks = new[] { 51, 67 },
                    Masks = { { 51, 0xF0F0000000000000UL }, { 67, 0xF0F0UL } },
                });
            return list.ToArray();
        }

        static int fails, passes;
        static Action<string> say;

        static void Verdict(bool ok, string name, string detail = null)
        {
            if (ok) passes++; else fails++;
            say((ok ? "  PASS  " : "  FAIL  ") + name + (detail != null ? " (" + detail + ")" : ""));
        }

        static string G(double v) { return v.ToString("R", CultureInfo.InvariantCulture); }

        sealed class Pins
        {
            public Dictionary<string, string> Files = new Dictionary<string, string>(StringComparer.Ordinal);
            public Dictionary<string, KeyValuePair<int, string>> Sets = new Dictionary<string, KeyValuePair<int, string>>(StringComparer.Ordinal);
            public Dictionary<string, string> Builds = new Dictionary<string, string>(StringComparer.Ordinal);
            public Dictionary<string, bool> SetVerified = new Dictionary<string, bool>(StringComparer.Ordinal);
        }

        public static int Run(string corpus, string dumpDir, Action<string> output)
        {
            say = output;
            fails = 0;
            passes = 0;
            string pinsPath = FindPins();
            if (pinsPath == null)
            {
                Verdict(false, "pins: Tools/world-audit/tests/corpus_hashes.json not found");
                return Finish();
            }
            Pins pins = LoadPins(pinsPath);
            if (!string.IsNullOrEmpty(dumpDir))
            {
                Directory.CreateDirectory(dumpDir);
                WriteSideChunkDump(dumpDir);
            }
            bool allTiles = Environment.GetEnvironmentVariable("WMV_TERRAIN_ALL_TILES") == "1";

            foreach (string client in new[] { "wow", "wow_classic_era", "wow_classic", "wow_classic_beta" })
            {
                say("-- terrain, " + client);
                string folder = Path.Combine(corpus, "corpus", client, "azeroth");
                string wdtRel = "wdt/" + client + "/" + AzerothWdt + ".bin";
                string problem = CheckPinned(corpus, pins, wdtRel);
                Verdict(problem == null, client + ": Azeroth WDT " + AzerothWdt + " has its pinned bytes", problem ?? Sha256(Path.Combine(corpus, wdtRel)).Substring(0, 16));
                if (problem != null)
                    return Finish();
                WdtFile wdt = WdtParser.Parse(File.ReadAllBytes(Path.Combine(corpus, wdtRel)), client + " azeroth.wdt");
                int[] window = ReadWindow(Path.Combine(folder, "corpus.json"));
                say(string.Format("    corpus window x {0}..{2}, y {1}..{3}", window[0], window[1], window[2], window[3]));

                foreach (Case k in Cases.Where(c => c.Client == client))
                {
                    if (k.X < window[0] || k.X > window[2] || k.Y < window[1] || k.Y > window[3])
                    {
                        say(string.Format("  SKIP  {0} {1}_{2}: outside this client's corpus window (not a pass)", client, k.X, k.Y));
                        continue;
                    }
                    RunCase(corpus, pins, wdt, folder, k, dumpDir);
                }
                if (allTiles)
                    RunWindow(corpus, pins, wdt, folder, client, window, dumpDir);
            }
            return Finish();
        }

        static int Finish()
        {
            say(string.Format("TerrainCorpusTests: {0} passed, {1} failed", passes, fails));
            return fails;
        }

        static void RunCase(string corpus, Pins pins, WdtFile wdt, string folder, Case k, string dumpDir)
        {
            string tag = k.Client + " " + k.X + "_" + k.Y;
            WdtTileFiles files = default(WdtTileFiles);
            bool named = wdt.TileExists(k.X, k.Y) && wdt.TryGetTileFiles(k.X, k.Y, out files);
            uint root = named ? files[WdtTileFileRole.RootAdt] : 0;
            uint minimap = named ? files[WdtTileFileRole.Minimap] : 0;
            Verdict(named && root == k.Root, tag + ": the WDT's MAID word 0 names root " + k.Root + " as the design pins",
                    "MAIN " + (wdt.TileExists(k.X, k.Y) ? "set" : "clear") + ", MAID root " + root + ", minimap (word 7) " + minimap);
            if (root == 0)
                return;
            string rel = "corpus/" + k.Client + "/azeroth/" + root + ".bin";
            string problem = CheckPinned(corpus, pins, rel);
            string path = Path.Combine(corpus, rel);
            Verdict(problem == null, tag + ": root " + root + " has its pinned bytes", problem ?? (new FileInfo(path).Length + " B, sha256 " + Sha256(path)));
            if (problem != null)
                return;
            byte[] bytes = File.ReadAllBytes(path);

            // Timing: 5 runs of parse + build (the first one includes JIT).
            var parseMs = new List<double>();
            var buildMs = new List<double>();
            AdtRoot parsed = null;
            TerrainTileMesh tile = null;
            for (int run = 0; run < 5; run++)
            {
                var sw = Stopwatch.StartNew();
                parsed = AdtParser.ParseRoot(bytes, k.X, k.Y, tag + " root");
                double p = sw.Elapsed.TotalMilliseconds;
                sw.Restart();
                tile = TerrainMeshBuilder.Build(parsed, k.X, k.Y, WorldToUnity.TileOrigin(k.X, k.Y));
                double b = sw.Elapsed.TotalMilliseconds;
                parseMs.Add(p);
                buildMs.Add(b);
            }
            say(string.Format(CultureInfo.InvariantCulture, "    timing {0}: parse ms {1}; build ms {2}; build median {3:0.00} ms",
                              tag, string.Join(" ", parseMs.Select(v => v.ToString("0.00", CultureInfo.InvariantCulture))),
                              string.Join(" ", buildMs.Select(v => v.ToString("0.00", CultureInfo.InvariantCulture))), Median(buildMs)));

            int withTriangles = tile.Chunks.Count(c => c.Triangles > 0);
            Verdict(tile.ChunksBuilt == 256 && tile.ChunksSkipped == 0 && tile.ChunksUnsupported == 0 && tile.Vertices == 145 * withTriangles &&
                    tile.Triangles == k.Triangles && tile.HoleCells == k.HoleCells && tile.Triangles == 65536 - 4 * tile.HoleCells,
                    tag + ": " + k.HoleCells + " hole cells, " + k.Triangles + " triangles, 256 chunks built",
                    string.Format("built {0}, skipped {1}, unsupported {2}, vertices {3}, triangles {4}, hole cells {5}, mesh bytes {6}",
                                  tile.ChunksBuilt, tile.ChunksSkipped, tile.ChunksUnsupported, tile.Vertices, tile.Triangles, tile.HoleCells, tile.MeshBytes));
            int[] holeChunks = tile.Chunks.Where(c => c.HoleCells > 0).Select(c => c.Index).ToArray();
            bool masks = k.Masks.All(m => tile.Chunks[m.Key].HoleMask == m.Value);
            Verdict(holeChunks.SequenceEqual(k.HoleChunks) && masks, tag + ": hole chunks and pinned masks",
                    holeChunks.Length == 0 ? "none" : string.Join(", ", holeChunks.Select(n => n + ":0x" + tile.Chunks[n].HoleMask.ToString("X", CultureInfo.InvariantCulture))));
            if (k.X == 32 && k.Y == 50)
                Verdict(tile.Vertices == 37120, tag + ": 37,120 vertices");

            CheckGeometry(tag, tile, parsed, true);
            CheckIdentity(tag, parsed, k.X, k.Y);
            if (k.Client == "wow" && k.X == 34 && k.Y == 46)
            {
                // Referenced-vertex bounds: chunk 145's unreferenced hole-cell vertices lie far below the drawn ground.
                TerrainChunkMesh c145 = tile.Chunks[145];
                float all = float.PositiveInfinity;
                for (int v = 0; v < 145; v++) all = Math.Min(all, c145.Positions[3 * v + 1]);
                Verdict(c145.BoundsMin[1] - all > 30, tag + ": chunk 145 bounds come from referenced vertices only",
                        string.Format(CultureInfo.InvariantCulture, "referenced min y {0:0.00}, all-vertex min y {1:0.00}, difference {2:0.00} yd", c145.BoundsMin[1], all, c145.BoundsMin[1] - all));
                Verdict(tile.Chunks[4].Triangles == 0 && tile.Chunks[4].BoundsMin == null && tile.Chunks[20].Triangles == 0,
                        tag + ": all-hole chunks 4 and 20 have no triangles and no bounds");
            }
            say(string.Format(CultureInfo.InvariantCulture, "    {0}: bounds ({1:0.###}, {2:0.###}, {3:0.###})..({4:0.###}, {5:0.###}, {6:0.###}), minimap {7}{8}",
                              tag, tile.BoundsMin[0], tile.BoundsMin[1], tile.BoundsMin[2], tile.BoundsMax[0], tile.BoundsMax[1], tile.BoundsMax[2], minimap,
                              File.Exists(Path.Combine(folder, minimap + ".bin")) ? " (in the corpus)" : " (not in the corpus)"));

            if (!string.IsNullOrEmpty(dumpDir))
                WriteDump(dumpDir, pins, k.Client, tile, root, minimap, bytes);
        }

        static void CheckGeometry(string tag, TerrainTileMesh tile, AdtRoot parsed, bool verbose)
        {
            int tris, points, compared, defaults;
            double cy;
            string p = TerrainMeshTests.CellStructureProblem(tile, out tris, out cy);
            Verdict(p == null && tris == tile.Triangles, tag + ": cell structure, every |Cross.y - U^2/2| <= 1e-3", p ?? "max " + G(cy) + " over " + tris + " triangles");
            p = TerrainMeshTests.BorderProblem(tile, out points);
            Verdict(p == null, tag + ": positions x/z and normals bit-equal at every chunk-border copy", p ?? points + " shared points");
            double ne = TerrainMeshTests.NormalError(tile, parsed, out compared, out defaults);
            Verdict(ne <= 1e-6, tag + ": normals equal an independent tile-wide recomputation within 1e-6",
                    "max " + G(ne) + " over " + compared + " vertices, " + defaults + " without a kept triangle");
            bool exact;
            double pe = TerrainMeshTests.LocalPrecision(tile, parsed, out exact);
            Verdict(pe <= 2.1e-5 && exact && TerrainMeshTests.FirstRowColumnZero(tile),
                    tag + ": stored x/z within 2.1e-5 yd of the double origin-local position and the float cast of it bit for bit (x, y, z), first row/column exactly 0",
                    "max " + G(pe) + (exact ? ", bit-exact" : ", NOT bit-exact"));
            float minUp = 1;
            foreach (TerrainChunkMesh c in tile.Chunks)
                if (c.State == TerrainChunkState.Built)
                    for (int k = 0; k < 145; k++) minUp = Math.Min(minUp, c.Normals[3 * k + 1]);
            if (verbose)
                say("    " + tag + ": steepest normal y " + G(minUp));
        }

        static void CheckIdentity(string tag, AdtRoot parsed, int tx, int ty)
        {
            double worst = 0;
            int genX = 0, genY = 0, decoded = 0;
            for (int n = 0; n < 256; n++)
            {
                AdtRootChunk mc = parsed.Mcnk[n];
                if (mc.Status != WorldDataStatus.Decoded) continue;
                decoded++;
                double ex, ey;
                TerrainCoordinates.ChunkOrigin(tx, ty, n % 16, n / 16, out ex, out ey);
                worst = Math.Max(worst, Math.Max(Math.Abs(mc.StoredPosition.A - ex), Math.Abs(mc.StoredPosition.B - ey)));
                if (mc.StoredPosition.A == TerrainMeshTests.StoredGenerator(ty, n / 16)) genX++;
                if (mc.StoredPosition.B == TerrainMeshTests.StoredGenerator(tx, n % 16)) genY++;
            }
            Verdict(worst <= TerrainMeshBuilder.IdentityTolerance && genX == decoded && genY == decoded,
                    tag + ": stored chunk x/y within the identity tolerance and reproduced bit-exactly by the float32 generator",
                    "max deviation " + G(worst) + " yd; generator equals stored x " + genX + "/" + decoded + ", y " + genY + "/" + decoded);
        }

        static void RunWindow(string corpus, Pins pins, WdtFile wdt, string folder, string client, int[] window, string dumpDir)
        {
            int tiles = 0, ok = 0;
            var problems = new List<string>();
            double maxNormal = 0, maxPrecision = 0, maxCross = 0;
            for (int y = window[1]; y <= window[3]; y++)
                for (int x = window[0]; x <= window[2]; x++)
                {
                    WdtTileFiles f;
                    if (!wdt.TileExists(x, y) || !wdt.TryGetTileFiles(x, y, out f)) continue;
                    uint root = f[WdtTileFileRole.RootAdt];
                    string rel = "corpus/" + client + "/azeroth/" + root + ".bin";
                    string path = Path.Combine(corpus, rel);
                    if (!File.Exists(path)) continue;
                    tiles++;
                    string pin = CheckPinned(corpus, pins, rel);
                    if (pin != null) { problems.Add(x + "_" + y + ": " + pin); continue; }
                    byte[] bytes = File.ReadAllBytes(path);
                    AdtRoot parsed = AdtParser.ParseRoot(bytes, x, y);
                    TerrainTileMesh tile = TerrainMeshBuilder.Build(parsed, x, y, WorldToUnity.TileOrigin(x, y));
                    int tris, points, compared, defaults;
                    double cy;
                    string p = TerrainMeshTests.CellStructureProblem(tile, out tris, out cy)
                               ?? TerrainMeshTests.BorderProblem(tile, out points);
                    double ne = TerrainMeshTests.NormalError(tile, parsed, out compared, out defaults);
                    bool exact;
                    double pe = TerrainMeshTests.LocalPrecision(tile, parsed, out exact);
                    maxNormal = Math.Max(maxNormal, ne);
                    maxPrecision = Math.Max(maxPrecision, pe);
                    maxCross = Math.Max(maxCross, cy);
                    if (p == null && tile.ChunksBuilt == 256 && tile.Triangles == 65536 - 4 * tile.HoleCells && ne <= 1e-6 && pe <= 2.1e-5 && exact &&
                        TerrainMeshTests.FirstRowColumnZero(tile))
                        ok++;
                    else
                        problems.Add(x + "_" + y + ": " + (p ?? "counts/normals/precision" + (exact ? "" : " (stored positions not bit-exact)")));
                    if (!string.IsNullOrEmpty(dumpDir))
                        WriteDump(dumpDir, pins, client, tile, root, f[WdtTileFileRole.Minimap], bytes);
                }
            Verdict(tiles > 0 && ok == tiles, client + ": every root tile of the corpus window builds with the invariants",
                    string.Format("{0}/{1} tiles; max normal error {2}, max precision {3}, max |Cross.y - U^2/2| {4}{5}", ok, tiles, G(maxNormal), G(maxPrecision), G(maxCross),
                                  problems.Count > 0 ? "; " + string.Join("; ", problems.Take(5)) : ""));
        }

        static void WriteDump(string dumpDir, Pins pins, string client, TerrainTileMesh tile, uint root, uint minimap, byte[] bytes)
        {
            TerrainDumpTile d = TerrainDump.FromMesh(tile, "runner");
            string build;
            pins.Builds.TryGetValue(client, out build);
            d.Meta.Add(new KeyValuePair<string, string>("client", client));
            d.Meta.Add(new KeyValuePair<string, string>("build", build ?? "unknown"));
            d.Meta.Add(new KeyValuePair<string, string>("mapId", "0"));
            d.Meta.Add(new KeyValuePair<string, string>("wdtFileDataID", AzerothWdt.ToString(CultureInfo.InvariantCulture)));
            d.Meta.Add(new KeyValuePair<string, string>("rootFileDataID", root.ToString(CultureInfo.InvariantCulture)));
            d.Meta.Add(new KeyValuePair<string, string>("minimapFileDataID", minimap.ToString(CultureInfo.InvariantCulture)));
            d.Meta.Add(new KeyValuePair<string, string>("rootBytes", bytes.Length.ToString(CultureInfo.InvariantCulture)));
            using (var sha = SHA256.Create())
                d.Meta.Add(new KeyValuePair<string, string>("rootSha256", Convert.ToHexString(sha.ComputeHash(bytes)).ToLowerInvariant()));
            using (var md5 = MD5.Create())
                d.Meta.Add(new KeyValuePair<string, string>("rootMd5", Convert.ToHexString(md5.ComputeHash(bytes)).ToLowerInvariant()));
            string name = string.Format(CultureInfo.InvariantCulture, "runner_{0}_{1}_{2}.txt", client, tile.TileX, tile.TileY);
            File.WriteAllText(Path.Combine(dumpDir, name), TerrainDump.ToText(d), new UTF8Encoding(false));
        }

        /// <summary>The synthetic root with malformed MCNR/MCCV chunks (TerrainMeshTests.SideChunkRoot, tile 32_50)
        /// and the builder's dump of it, so Tools/world-audit/tests/test_terrain_tools.py can check that the Python
        /// reference skips the same chunks as the builder.</summary>
        static void WriteSideChunkDump(string dumpDir)
        {
            byte[] bytes = TerrainMeshTests.SideChunkRoot(32, 50);
            TerrainTileMesh tile = TerrainMeshTests.BuildBytes(bytes, 32, 50);
            TerrainDumpTile d = TerrainDump.FromMesh(tile, "runner");
            d.Meta.Add(new KeyValuePair<string, string>("client", "synthetic"));
            d.Meta.Add(new KeyValuePair<string, string>("rootFile", "synthetic_sidechunks_32_50.bin"));
            File.WriteAllBytes(Path.Combine(dumpDir, "synthetic_sidechunks_32_50.bin"), bytes);
            File.WriteAllText(Path.Combine(dumpDir, "runner_synthetic_sidechunks_32_50.txt"), TerrainDump.ToText(d), new UTF8Encoding(false));
            Verdict(tile.ChunksSkipped == 4 && tile.ChunksBuilt == 252, "synthetic: the malformed MCNR/MCCV root and its dump are written for the Python reference compare",
                    "skipped " + string.Join(", ", tile.Chunks.Where(c => c.State == TerrainChunkState.Skipped).Select(c => c.Index)));
        }

        // ================================================================================== inputs

        static int[] ReadWindow(string corpusJson)
        {
            using (JsonDocument doc = JsonDocument.Parse(File.ReadAllText(corpusJson)))
                return doc.RootElement.GetProperty("window").EnumerateArray().Select(e => e.GetInt32()).ToArray();
        }

        static string FindPins()
        {
            string env = Environment.GetEnvironmentVariable("WMV_WORLD_AUDIT_PINS");
            if (!string.IsNullOrEmpty(env) && File.Exists(env))
                return env;
            foreach (string start in new[] { Environment.CurrentDirectory, AppContext.BaseDirectory })
            {
                var d = new DirectoryInfo(start);
                while (d != null)
                {
                    string p = Path.Combine(d.FullName, "Tools", "world-audit", "tests", "corpus_hashes.json");
                    if (File.Exists(p))
                        return p;
                    d = d.Parent;
                }
            }
            return null;
        }

        static Pins LoadPins(string path)
        {
            var pins = new Pins();
            using (JsonDocument doc = JsonDocument.Parse(File.ReadAllText(path)))
            {
                foreach (JsonProperty f in doc.RootElement.GetProperty("files").EnumerateObject())
                    pins.Files[f.Name] = f.Value.GetString();
                foreach (JsonProperty s in doc.RootElement.GetProperty("sets").EnumerateObject())
                    pins.Sets[s.Name] = new KeyValuePair<int, string>(s.Value.GetProperty("files").GetInt32(), s.Value.GetProperty("digest").GetString());
                JsonElement cb;
                if (doc.RootElement.TryGetProperty("client_builds", out cb))
                    foreach (JsonProperty b in cb.EnumerateObject())
                        pins.Builds[b.Name] = b.Value.GetString();
            }
            return pins;
        }

        /// <summary>Null when the file at `rel` (corpus-relative, '/' separators) has its own pinned SHA-256 or
        /// lies in a pinned file set whose digest holds; otherwise what is wrong.</summary>
        static string CheckPinned(string corpus, Pins pins, string rel)
        {
            string path = Path.Combine(corpus, rel);
            if (!File.Exists(path))
                return "missing " + rel;
            string want;
            if (pins.Files.TryGetValue(rel, out want))
                return Sha256(path) == want ? null : "changed " + rel + " (pinned sha256 " + want.Substring(0, 16) + ")";
            string dir = rel.Substring(0, rel.LastIndexOf('/'));
            KeyValuePair<int, string> set;
            if (!pins.Sets.TryGetValue(dir, out set))
                return rel + " is not pinned";
            bool verified;
            if (!pins.SetVerified.TryGetValue(dir, out verified))
            {
                string full = Path.Combine(corpus, dir);
                var names = Directory.GetFiles(full, "*.bin").Select(Path.GetFileName).OrderBy(n => n, StringComparer.Ordinal).ToList();
                var sb = new StringBuilder();
                foreach (string n in names) sb.Append(n).Append(' ').Append(Sha256(Path.Combine(full, n))).Append('\n');
                string digest;
                using (var h = SHA256.Create())
                    digest = Convert.ToHexString(h.ComputeHash(Encoding.ASCII.GetBytes(sb.ToString()))).ToLowerInvariant();
                verified = names.Count == set.Key && digest == set.Value;
                pins.SetVerified[dir] = verified;
                say(string.Format("    pinned set {0}: {1} files, digest {2}", dir, names.Count, verified ? "holds" : "CHANGED"));
            }
            return verified ? null : "the pinned set " + dir + " changed";
        }

        static string Sha256(string path)
        {
            using (var s = SHA256.Create())
            using (FileStream f = File.OpenRead(path))
                return Convert.ToHexString(s.ComputeHash(f)).ToLowerInvariant();
        }

        static double Median(List<double> v)
        {
            var s = v.OrderBy(x => x).ToList();
            return s.Count % 2 == 1 ? s[s.Count / 2] : (s[s.Count / 2 - 1] + s[s.Count / 2]) / 2;
        }
    }
}
