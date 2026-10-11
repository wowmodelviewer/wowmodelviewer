// WorldCorpusValidation.cs
//
// The world parsers against REAL client bytes: the pinned audit corpus (docs/world-viewer/00-audit.md,
// data in the git-ignored build/world-audit-data, located by WMV_WORLD_AUDIT_CORPUS).
//
//   1. The inputs are checked first against Tools/world-audit/tests/corpus_hashes.json (243 files by
//      SHA-256, 19 file-set digests). A changed input stops the comparison: it is never accepted silently.
//   2. Everything below is computed from the original files by the C# parsers -- never from the
//      Python audit's parsed results. The window of each client is found from its own WDT's MAID.
//   3. It recomputes the audit's tables and verdicts with the C# data: T2 (WDT chunk orders, MPHD
//      flags), T3 (MAID word counts), the MAIN/MPHD flag census (wdt_flags.py), the MAID roles by
//      content (maid_content.py), the ADT structure census behind T6 (audit_adt.py), every coords.py
//      verdict (vertex order, seams, inner vertices, placement height, holes, alpha orientation,
//      nibble order) and the rotation.py / mldx.py sweeps -- and asserts the rules
//      test_corpus_rules.py asserts.
//   4. It writes all of it to one JSON (default <corpus>/results/csharp_parsers.json), which
//      Tools/world-audit/compare_csharp.py compares with the Python results key by key.
//   5. It measures parse time, allocations and retained size per part type.
//
// Runs only from the console runner (Tests/Runner); uses System.Text.Json and file IO, which the
// Unity parsing layer itself never does.

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
    public static class WorldCorpusValidation
    {
        static readonly string[] Clients = { "wow", "wow_classic_beta", "wow_classic_era", "wow_classic" };
        const double T = TerrainCoordinates.TileSize;
        const double C = TerrainCoordinates.ChunkSize;
        const double U = TerrainCoordinates.UnitSize;

        static int fails, passes;
        static Action<string> say;
        static readonly List<object> checks = new List<object>();

        static void Verdict(bool ok, string name, string detail = null)
        {
            if (ok) passes++; else fails++;
            string line = (ok ? "  PASS  " : "  FAIL  ") + name + (detail != null ? " (" + detail + ")" : "");
            say(line);
            checks.Add(new Dictionary<string, object> { { "name", name }, { "result", ok ? "PASS" : "FAIL" }, { "detail", detail } });
        }

        public static int Run(string root, string outPath, Action<string> output)
        {
            say = output;
            fails = 0;
            passes = 0;
            checks.Clear();
            outPath = outPath ?? Path.Combine(root, "results", "csharp_parsers.json");
            var result = new Dictionary<string, object>();
            result["corpus"] = root;
            result["generated_by"] = "Tools/UnityRendererProject/Tests/WorldCorpusValidation.cs";

            // ---- 1. pins
            string pinsPath = FindPins();
            if (pinsPath == null)
            {
                Verdict(false, "pins: Tools/world-audit/tests/corpus_hashes.json not found");
                return Finish(result, outPath);
            }
            var pins = VerifyPins(root, pinsPath);
            result["pins"] = pins;
            if (fails > 0)
            {
                say("The corpus differs from the pinned bytes: no comparison is run (re-extract and review before re-pinning).");
                return Finish(result, outPath);
            }

            // ---- 2/3. per client
            var clients = new Dictionary<string, object>();
            var perf = new Dictionary<string, object>();
            foreach (string c in Clients)
            {
                say("== " + c);
                var cr = new Dictionary<string, object>();
                cr["wdt_census"] = WdtCensus(root, c);
                Window w = LoadWindow(root, c, cr);
                cr["maid_roles"] = MaidRoles(root, c, w);
                cr["adt"] = AdtCensus(w);
                cr["coords"] = Coords(w);
                cr["seams"] = SeamReport(w);
                cr["dedup"] = DedupReport(w);
                cr["liquids"] = LiquidReport(w);
                cr["alpha_edges"] = AlphaEdges(w);
                perf[c] = Performance(root, c, w);
                if (c == "wow")
                {
                    cr["rotation"] = RotationSweep(root, w);
                    cr["mldx"] = MldxSweep(root, w);
                }
                clients[c] = cr;
            }
            result["clients"] = clients;
            result["performance"] = perf;
            return Finish(result, outPath);
        }

        static int Finish(Dictionary<string, object> result, string outPath)
        {
            result["checks"] = checks;
            result["summary"] = new Dictionary<string, object> { { "pass", passes }, { "fail", fails } };
            Directory.CreateDirectory(Path.GetDirectoryName(outPath));
            File.WriteAllText(outPath, JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
            say(string.Format("WorldCorpusValidation: {0} passed, {1} failed; results in {2}", passes, fails, outPath));
            return fails;
        }

        // ================================================================================== pins

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

        static string Sha(string path)
        {
            using (var s = SHA256.Create())
            using (var f = File.OpenRead(path))
                return Convert.ToHexString(s.ComputeHash(f)).ToLowerInvariant();
        }

        static Dictionary<string, object> VerifyPins(string root, string pinsPath)
        {
            var doc = JsonDocument.Parse(File.ReadAllText(pinsPath));
            int files = 0, filesOk = 0, sets = 0, setsOk = 0, setFiles = 0;
            var bad = new List<string>();
            foreach (JsonProperty f in doc.RootElement.GetProperty("files").EnumerateObject())
            {
                files++;
                string p = Path.Combine(root, f.Name);
                if (!File.Exists(p)) { bad.Add("missing " + f.Name); continue; }
                if (Sha(p) == f.Value.GetString()) filesOk++; else bad.Add("changed " + f.Name);
            }
            foreach (JsonProperty s in doc.RootElement.GetProperty("sets").EnumerateObject())
            {
                sets++;
                string dir = Path.Combine(root, s.Name);
                if (!Directory.Exists(dir)) { bad.Add("missing set " + s.Name); continue; }
                var names = Directory.GetFiles(dir, "*.bin").Select(Path.GetFileName).OrderBy(n => n, StringComparer.Ordinal).ToList();
                var sb = new StringBuilder();
                foreach (string n in names) sb.Append(n).Append(' ').Append(Sha(Path.Combine(dir, n))).Append('\n');
                string digest;
                using (var h = SHA256.Create())
                    digest = Convert.ToHexString(h.ComputeHash(Encoding.ASCII.GetBytes(sb.ToString()))).ToLowerInvariant();
                setFiles += names.Count;
                if (names.Count == s.Value.GetProperty("files").GetInt32() && digest == s.Value.GetProperty("digest").GetString()) setsOk++;
                else bad.Add("changed set " + s.Name);
            }
            var builds = new Dictionary<string, object>();
            JsonElement cb;
            if (doc.RootElement.TryGetProperty("client_builds", out cb))
                foreach (JsonProperty b in cb.EnumerateObject()) builds[b.Name] = b.Value.GetString();
            Verdict(files == 243 && filesOk == files, "pins: every pinned file has its audited SHA-256", filesOk + " of " + files);
            Verdict(sets == 19 && setsOk == sets, "pins: every file set has its audited digest", setsOk + " of " + sets + " sets, " + setFiles + " files");
            foreach (string b in bad.Take(20)) say("    " + b);
            return new Dictionary<string, object> { { "pins_file", pinsPath }, { "files", files }, { "files_ok", filesOk }, { "sets", sets },
                                                    { "sets_ok", setsOk }, { "set_files", setFiles }, { "problems", bad }, { "client_builds", builds } };
        }

        // ================================================================================== WDT census (T2, T3, wdt_flags)

        static Dictionary<string, object> WdtCensus(string root, string client)
        {
            string dir = Path.Combine(root, "wdt", client);
            var orders = new SortedDictionary<string, int>(StringComparer.Ordinal);
            var mphdFlags = new SortedDictionary<string, int>(StringComparer.Ordinal);
            var maidNonzero = new int[8];
            var maidZero = new int[8];
            var mainVsMaid = new SortedDictionary<string, int>(StringComparer.Ordinal);
            var bits = new SortedDictionary<string, int>(StringComparer.Ordinal);
            var bit1 = new SortedDictionary<string, int>(StringComparer.Ordinal);
            var word7 = new SortedDictionary<string, int>(StringComparer.Ordinal);
            var modf = new SortedDictionary<string, int>(StringComparer.Ordinal);
            var kinds = new SortedDictionary<string, int>(StringComparer.Ordinal);
            var issues = new List<string>();
            int parsed = 0, failed = 0, bitMismatch = 0, flag2Named = 0, unseenMain = 0;
            foreach (string path in Directory.GetFiles(dir, "*.bin").OrderBy(p => p, StringComparer.Ordinal))
            {
                string name = Path.GetFileNameWithoutExtension(path);
                if (!name.All(char.IsDigit))
                    continue;
                byte[] data = File.ReadAllBytes(path);
                WdtFile w;
                try { w = WdtParser.Parse(data, client + "/" + name + ".wdt"); }
                catch (WorldParseException e) { failed++; issues.Add(e.Message); continue; }
                parsed++;
                foreach (WorldIssue i in w.Issues) issues.Add(i.ToString());
                Inc(orders, GroupedTags(w.Chunks));
                Inc(mphdFlags, w.Header.Flags.ToString(CultureInfo.InvariantCulture));
                Inc(kinds, w.Kind.ToString());
                int tiles = w.TileCount;
                if (w.Maid != null)
                    for (int t = 0; t < 4096; t++)
                    {
                        bool any = false;
                        for (int k = 0; k < 8; k++) any |= w.Maid[t * 8 + k] != 0;
                        if (!any) continue;
                        for (int k = 0; k < 8; k++)
                            if (w.Maid[t * 8 + k] != 0) maidNonzero[k]++; else maidZero[k]++;
                    }
                for (int t = 0; t < 4096; t++)
                {
                    bool named = false;
                    if (w.Maid != null) for (int k = 0; k < 8; k++) named |= w.Maid[t * 8 + k] != 0;
                    if (w.HasMaid && ((w.Main[t].Flags & 1) != 0) != named) bitMismatch++;
                    if (w.HasMaid && (w.Main[t].Flags & 2) != 0 && named) flag2Named++;
                    if (w.Main[t].Flags > 2 || w.Main[t].Word1Raw != 0) unseenMain++;
                    Inc(mainVsMaid, string.Format("{0}: flag 0x{1:X}, {2}", w.HasMaid ? "WDT with MAID" : "WDT without MAID", w.Main[t].Flags,
                                                  named ? "MAID names files" : "no MAID words"));
                }
                for (int b = 0; b < 32; b++)
                    if ((w.Header.Flags & (1u << b)) != 0) Inc(bits, "0x" + (1u << b).ToString("X"));
                bool wmoOnly = w.GlobalWmos.Length > 0 && tiles == 0;
                Inc(bit1, string.Format("bit 0x1 {0}, {1}", w.Header.WmoOnlyFlag ? "set" : "clear", wmoOnly ? "WMO-only" : "has tiles or no MODF"));
                Inc(word7, string.Format("word 7 {0}, {1}", w.Header.Word7Raw != 0 ? "non-zero" : "zero", wmoOnly ? "WMO-only" : "not WMO-only"));
                bool hasMwmo = w.Chunks.Any(k => k.Tag == "MWMO");
                foreach (WmoPlacement p in w.GlobalWmos)
                    Inc(modf, string.Format("{0}; rotation ({1}, {2}, {3}); scale {4}; flags 0x{5:X}", hasMwmo ? "MWMO path" : "FileDataID name",
                                            Py1(p.Rotation.A), Py1(p.Rotation.B), Py1(p.Rotation.C), p.ScaleRaw, p.Flags));
                if (w.GlobalWmos.Length > 0 && tiles > 0) Inc(modf, "WDT with both MODF and tiles");
            }
            Verdict(failed == 0 && parsed > 0, client + " WDT: every extracted WDT parses", parsed + " parsed, " + failed + " failed");
            Verdict(issues.Count == 0, client + " WDT: no issue in any WDT (every audited rule holds)", issues.Count + " issue(s)" + (issues.Count > 0 ? ": " + issues[0] : ""));
            // The same predicates as the parser and test_corpus_rules.py: bit 0x1, bit 0x2 (not the census text).
            Verdict(bitMismatch == 0, client + " WDT: MAIN bit 0x1 <=> MAID names files (test_corpus_rules)", bitMismatch + " mismatching entries");
            Verdict(flag2Named == 0, client + " WDT: MAIN bit 0x2 never comes with MAID words", flag2Named + " entries");
            Verdict(unseenMain == 0, client + " WDT: MAIN holds only the values 0, 1, 2 with a zero second word", unseenMain + " other entries");
            var maid = new Dictionary<string, object>();
            for (int k = 0; k < 8; k++) maid[k.ToString()] = new Dictionary<string, int> { { "nonzero", maidNonzero[k] }, { "zero", maidZero[k] } };
            return new Dictionary<string, object>
            {
                { "wdts_parsed", parsed }, { "wdts_failed", failed }, { "issues", issues }, { "chunk_orders", orders },
                { "mphd_flag_values", mphdFlags }, { "maid_words", maid }, { "kinds", kinds },
                { "main_vs_maid", mainVsMaid }, { "mphd_bits_seen", bits }, { "mphd_bit_0x1", bit1 }, { "mphd_word_7", word7 }, { "wdt_modf", modf },
            };
        }

        /// <summary>Python repr of round(v, 1) for the values these files hold.</summary>
        static string Py1(float v)
        {
            double r = Math.Round((double)v, 1, MidpointRounding.ToEven);
            string s = r.ToString("0.0###############", CultureInfo.InvariantCulture);
            if (r == 0 && (BitConverter.DoubleToInt64Bits(r) < 0 || (v < 0 && r == 0))) s = "-0.0";
            return s;
        }

        static string GroupedTags(WorldChunk[] chunks)
        {
            var tags = new List<string>();
            foreach (WorldChunk k in chunks)
                if (tags.Count == 0 || tags[tags.Count - 1] != k.Tag) tags.Add(k.Tag);
            return string.Join(" ", tags);
        }

        // ================================================================================== the window

        sealed class Tile
        {
            public int X, Y;
            public uint[] Maid;
            public Dictionary<string, byte[]> Bytes = new Dictionary<string, byte[]>();
            public AdtTile Parsed;
        }

        sealed class Chunk
        {
            public int Tx, Ty, Ix, Iy;
            public AdtRootChunk Root;
            public AdtTexChunk Tex;
            public float[] Heights;
        }

        sealed class Window
        {
            public string Client;
            public WdtFile Wdt;
            public List<Tile> Tiles = new List<Tile>();
            public Dictionary<(int, int, int, int), Chunk> Chunks = new Dictionary<(int, int, int, int), Chunk>();
            public string Folder;
        }

        static readonly string[] PartNames = { "root", "obj0", "obj1", "tex0", "lod" };

        static Window LoadWindow(string root, string client, Dictionary<string, object> cr)
        {
            var w = new Window { Client = client, Folder = Path.Combine(root, "corpus", client, "azeroth") };
            byte[] wdtBytes = File.ReadAllBytes(Path.Combine(root, "wdt", client, "775971.bin"));
            w.Wdt = WdtParser.Parse(wdtBytes, client + " azeroth.wdt");
            var partStates = new SortedDictionary<string, int>(StringComparer.Ordinal);
            var partIssues = new List<string>();
            for (int i = 0; i < 4096; i++)
            {
                if (!w.Wdt.Main[i].HasTile) continue;
                WdtTileFiles f;
                w.Wdt.TryGetTileFiles(i % 64, i / 64, out f);
                string rootPath = Path.Combine(w.Folder, f.FileDataIds[0] + ".bin");
                if (!File.Exists(rootPath)) continue;
                var t = new Tile { X = i % 64, Y = i / 64, Maid = f.FileDataIds };
                for (int k = 0; k < 5; k++)
                {
                    uint id = f.FileDataIds[k];
                    string p = Path.Combine(w.Folder, id + ".bin");
                    if (id != 0 && File.Exists(p)) t.Bytes[PartNames[k]] = File.ReadAllBytes(p);
                }
                t.Parsed = AdtParser.ParseTile(t.X, t.Y, f.FileDataIds, Get(t, "root"), Get(t, "tex0"), Get(t, "obj0"), Get(t, "obj1"), Get(t, "lod"));
                Inc(partStates, "root " + t.Parsed.Root.State); Inc(partStates, "tex0 " + t.Parsed.Tex0.State);
                Inc(partStates, "obj0 " + t.Parsed.Obj0.State); Inc(partStates, "obj1 " + t.Parsed.Obj1.State);
                Inc(partStates, "lod " + t.Parsed.Lod.State);
                CollectIssues(partIssues, t.Parsed);
                w.Tiles.Add(t);
                AdtRoot r = t.Parsed.Root.Value;
                AdtTex tex = t.Parsed.Tex0.Value;
                if (r == null) continue;
                for (int n = 0; n < 256; n++)
                {
                    AdtRootChunk rc = r.Mcnk[n];
                    w.Chunks[(t.X, t.Y, (int)rc.IndexX, (int)rc.IndexY)] = new Chunk
                    {
                        Tx = t.X, Ty = t.Y, Ix = (int)rc.IndexX, Iy = (int)rc.IndexY, Root = rc, Tex = tex != null ? tex.Mcnk[n] : null, Heights = rc.Heights,
                    };
                }
            }
            int malformed = partStates.Where(kv => kv.Key.EndsWith("Malformed")).Sum(kv => kv.Value);
            Verdict(w.Tiles.Count > 0 && malformed == 0, client + " window: every supplied ADT part parses",
                    w.Tiles.Count + " tiles; " + string.Join(", ", partStates.Select(kv => kv.Key + " " + kv.Value)));
            var unexpected = partIssues.Where(s => !s.StartsWith("Unsupported")).ToList();
            Verdict(unexpected.Count == 0, client + " window: no Malformed/Unexpected issue in any part",
                    unexpected.Count + " (" + partIssues.Count + " Unsupported notes)" + (unexpected.Count > 0 ? ": " + unexpected[0] : ""));
            cr["window"] = new Dictionary<string, object> { { "tiles", w.Tiles.Count }, { "chunks", w.Chunks.Count }, { "part_states", partStates },
                                                             { "issues", partIssues.GroupBy(s => s.Split(':')[0] + ": " + Tail(s)).ToDictionary(g => g.Key, g => g.Count()) } };
            return w;
        }

        static string Tail(string issue)
        {
            int k = issue.IndexOf(" at offset ", StringComparison.Ordinal);
            int c = k >= 0 ? issue.IndexOf(": ", k, StringComparison.Ordinal) : -1;
            string reason = c >= 0 ? issue.Substring(c + 2) : issue;
            return System.Text.RegularExpressions.Regex.Replace(reason, @"\d+", "N");
        }

        static void CollectIssues(List<string> into, AdtTile t)
        {
            if (t.Root.Value != null) into.AddRange(t.Root.Value.Issues.Select(i => i.ToString()));
            if (t.Tex0.Value != null) into.AddRange(t.Tex0.Value.Issues.Select(i => i.ToString()));
            if (t.Obj0.Value != null) into.AddRange(t.Obj0.Value.Issues.Select(i => i.ToString()));
            if (t.Obj1.Value != null) into.AddRange(t.Obj1.Value.Issues.Select(i => i.ToString()));
            if (t.Lod.Value != null) into.AddRange(t.Lod.Value.Issues.Select(i => i.ToString()));
            foreach (var e in new[] { t.Root.Error, t.Tex0.Error, t.Obj0.Error, t.Obj1.Error, t.Lod.Error })
                if (e != null) into.Add("Malformed part: " + e);
        }

        static byte[] Get(Tile t, string part) { byte[] b; return t.Bytes.TryGetValue(part, out b) ? b : null; }

        // ================================================================================== MAID roles (maid_content.py)

        static Dictionary<string, object> MaidRoles(string root, string client, Window w)
        {
            var roles = new SortedDictionary<string, SortedDictionary<string, int>>(StringComparer.Ordinal);
            foreach (Tile t in w.Tiles)
                for (int k = 0; k < 8; k++)
                {
                    if (!roles.ContainsKey(k.ToString())) roles[k.ToString()] = new SortedDictionary<string, int>(StringComparer.Ordinal);
                    uint id = t.Maid[k];
                    string role;
                    string p = Path.Combine(w.Folder, id + ".bin");
                    if (id == 0) role = "(zero)";
                    else if (!File.Exists(p)) role = "(not extracted)";
                    else role = RoleOf(File.ReadAllBytes(p));
                    Inc(roles[k.ToString()], role);
                }
            string[] expect = { "root (has MHDR)", "obj0 (has MDDF)", "obj1 (has MLDD)", "tex0 (has MAMP)", "lod (has MLHD)" };
            bool ok = true;
            for (int k = 0; k < 5; k++)
                ok &= roles[k.ToString()].Keys.All(r => r == expect[k] || r == "(zero)");
            Verdict(ok, client + " MAID: words 0-4 hold root, obj0, obj1, tex0, lod by content (no listfile)");
            var o = new Dictionary<string, object>();
            foreach (var kv in roles) o[kv.Key] = kv.Value;
            return o;
        }

        static readonly string[][] RoleByChunks =
        {
            new[] { "MHDR", "root" }, new[] { "MLHD", "lod" }, new[] { "MAMP", "tex0" }, new[] { "MDID", "tex0" },
            new[] { "MLDD", "obj1" }, new[] { "MLMD", "obj1" }, new[] { "MDDF", "obj0" }, new[] { "MODF", "obj0" },
        };

        static string RoleOf(byte[] b)
        {
            if (b.Length >= 20 && b[0] == 'B' && b[1] == 'L' && b[2] == 'P' && b[3] == '2')
            {
                uint wdt = BitConverter.ToUInt32(b, 12), hgt = BitConverter.ToUInt32(b, 16);
                return string.Format("BLP2 {0}x{1} compression {2} alpha type {3}", wdt, hgt, b[8], b[10]);
            }
            var ids = TolerantTags(b, 12);
            foreach (string[] rc in RoleByChunks)
                if (ids.Contains(rc[0])) return rc[1] + " (has " + rc[0] + ")";
            return "unknown: " + string.Join(" ", ids.Take(4));
        }

        static List<string> TolerantTags(byte[] b, int max)
        {
            var ids = new List<string>();
            long p = 0;
            while (p + 8 <= b.Length && ids.Count < max)
            {
                uint size = BitConverter.ToUInt32(b, (int)p + 4);
                if (p + 8 + size > b.Length) break;
                ids.Add(WorldChunkWalker.TagAt(b, (int)p));
                p += 8 + size;
            }
            return ids;
        }

        // ================================================================================== ADT census (audit_adt.py, T6)

        static Dictionary<string, object> AdtCensus(Window w)
        {
            var stats = new Dictionary<string, SortedDictionary<string, int>>();
            foreach (string s in new[] { "root_header_index_ok", "flags", "mcly_0x100_by_position", "root_n_layers_value",
                                         "root_alpha_shadow_offsets_all_zero", "mcsh_vs_root_flag1", "raw_alpha_spacing", "mcly_flags",
                                         "mddf_flags", "modf_flags" })
                stats[s] = new SortedDictionary<string, int>(StringComparer.Ordinal);
            var tiles = new SortedDictionary<string, object>(StringComparer.Ordinal);
            foreach (Tile t in w.Tiles)
            {
                var to = new Dictionary<string, object>();
                AdtRoot r = t.Parsed.Root.Value;
                AdtTex tex = t.Parsed.Tex0.Value;
                if (r != null)
                {
                    var subSets = new SortedDictionary<string, int>(StringComparer.Ordinal);
                    var holes = new SortedDictionary<string, int>(StringComparer.Ordinal);
                    var offsets = new SortedDictionary<string, int>(StringComparer.Ordinal);
                    var fit = new SortedDictionary<string, int>(StringComparer.Ordinal);
                    byte[] data = t.Bytes["root"];
                    var top = WorldChunkWalker.Walk(data, 0, data.Length, "root", "file");
                    var mcnk = top.Where(k => k.Tag == "MCNK").ToList();
                    for (int n = 0; n < 256; n++)
                    {
                        AdtRootChunk ch = r.Mcnk[n];
                        Inc(stats["root_header_index_ok"], (ch.IndexX == n % 16 && ch.IndexY == n / 16) ? "True" : "False");
                        var sub = WorldChunkWalker.Walk(data, mcnk[n].DataOffset + 128, mcnk[n].DataOffset + mcnk[n].Size, "root", "MCNK");
                        Inc(subSets, GroupedTags(sub.ToArray()));
                        bool hi = (ch.Flags & 0x10000) != 0;
                        Inc(holes, hi ? "flag_0x10000" : "no_flag_0x10000");
                        if (hi)
                        {
                            Inc(holes, ch.Holes.HighResolutionBits != 0 ? "high_mask_nonzero" : "high_mask_zero");
                            Inc(holes, ch.Holes.LowResolutionRaw != 0 ? "low_nonzero_with_flag" : "low_zero_with_flag");
                        }
                        else Inc(holes, ch.Holes.LowResolutionRaw != 0 ? "low_nonzero" : "low_zero");
                        var pos = new Dictionary<string, int>();
                        foreach (WorldChunk s in sub) pos[s.Tag] = s.HeaderOffset - mcnk[n].HeaderOffset;
                        foreach (var fld in new[] { ("ofs_height", 20, "MCVT"), ("ofs_normal", 24, "MCNR"), ("ofs_mccv", 116, "MCCV"),
                                                    ("ofs_mclv", 120, "MCLV"), ("ofs_snd", 88, "MCSE"), ("ofs_liquid", 96, "MCLQ") })
                        {
                            if ((fld.Item1 == "ofs_height" || fld.Item1 == "ofs_normal") && hi) continue;
                            uint v = BitConverter.ToUInt32(ch.RawHeader, fld.Item2);
                            int at;
                            if (pos.TryGetValue(fld.Item3, out at))
                                Inc(offsets, v == at ? fld.Item1 + "=" + fld.Item3 + " start" : string.Format("{0}!={1} start ({2} vs {3})", fld.Item1, fld.Item3, v, at));
                            else if (v != 0)
                                Inc(offsets, string.Format("{0}={1} but no {2}", fld.Item1, v, fld.Item3));
                        }
                        double px = ch.StoredPosition.A, py = ch.StoredPosition.B;
                        int tx = t.X, ty = t.Y, ix = (int)ch.IndexX, iy = (int)ch.IndexY;
                        FitInc(fit, "x=(32-ty)*T-iy*C, y=(32-tx)*T-ix*C", (32 - ty) * T - iy * C, (32 - tx) * T - ix * C, px, py);
                        FitInc(fit, "x=(32-tx)*T-ix*C, y=(32-ty)*T-iy*C", (32 - tx) * T - ix * C, (32 - ty) * T - iy * C, px, py);
                        FitInc(fit, "x=(tx-32)*T+ix*C, y=(ty-32)*T+iy*C", (tx - 32) * T + ix * C, (ty - 32) * T + iy * C, px, py);
                        Inc(stats["flags"], ch.Flags.ToString(CultureInfo.InvariantCulture));
                    }
                    var ro = new Dictionary<string, object> { { "top", TopList(r.Chunks) }, { "subchunk_sets", subSets }, { "holes", holes },
                                                              { "header_offsets", offsets }, { "position_fit", fit } };
                    if (r.Liquids != null && r.Liquids.Instances != null)
                    {
                        var lt = new SortedDictionary<string, int>(StringComparer.Ordinal);
                        var lo = new SortedDictionary<string, int>(StringComparer.Ordinal);
                        int inst = 0, withLiquid = 0;
                        for (int n = 0; n < 256; n++)
                        {
                            if (r.Liquids.Headers[n].LayerCount > 0) withLiquid++;
                            foreach (LiquidInstanceRaw li in r.Liquids.Instances[n]) { inst++; Inc(lt, li.Word0.ToString()); Inc(lo, li.Word2.ToString()); }
                        }
                        ro["mh2o"] = new Dictionary<string, object> { { "size", r.Liquids.Size }, { "chunks_with_liquid", withLiquid }, { "instances", inst },
                                                                      { "liquid_types", lt }, { "object_or_lvf", lo }, { "status", r.Liquids.Status.ToString() } };
                    }
                    to["root"] = ro;
                }
                if (tex != null)
                {
                    var subSets = new SortedDictionary<string, int>(StringComparer.Ordinal);
                    var alpha = new SortedDictionary<string, int>(StringComparer.Ordinal);
                    var perChunk = new SortedDictionary<string, int>(StringComparer.Ordinal);
                    byte[] data = t.Bytes["tex0"];
                    var top = WorldChunkWalker.Walk(data, 0, data.Length, "tex0", "file");
                    var mcnk = top.Where(k => k.Tag == "MCNK").ToList();
                    for (int n = 0; n < 256; n++)
                    {
                        AdtTexChunk ch = tex.Mcnk[n];
                        var sub = WorldChunkWalker.Walk(data, mcnk[n].DataOffset, mcnk[n].DataOffset + mcnk[n].Size, "tex0", "MCNK");
                        Inc(subSets, GroupedTags(sub.ToArray()));
                        Inc(perChunk, ch.Layers.Length.ToString());
                        for (int k = 0; k < ch.Layers.Length; k++)
                            Inc(stats["mcly_0x100_by_position"], string.Format("('{0}', {1})", k == 0 ? "first" : "later", (ch.Layers[k].Flags & 0x100) != 0 ? "True" : "False"));
                        if (r != null)
                        {
                            byte[] h = r.Mcnk[n].RawHeader;
                            Inc(stats["root_n_layers_value"], BitConverter.ToUInt32(h, 12).ToString());
                            bool allZero = BitConverter.ToUInt32(h, 36) == 0 && BitConverter.ToUInt32(h, 40) == 0 && BitConverter.ToUInt32(h, 44) == 0 &&
                                           BitConverter.ToUInt32(h, 48) == 0;
                            Inc(stats["root_alpha_shadow_offsets_all_zero"], allZero ? "True" : "False");
                            Inc(stats["mcsh_vs_root_flag1"], string.Format("({0}, {1})", ch.ShadowRaw != null ? "True" : "False",
                                                                          (r.Mcnk[n].Flags & 1) != 0 ? "True" : "False"));
                        }
                        if (ch.AlphaSize < 0)
                        {
                            if (ch.Layers.Length > 1) Inc(alpha, "layers>1 but no MCAL");
                            continue;
                        }
                        string verdict;
                        if (ch.Status == WorldDataStatus.Decoded && ch.Alpha != null)
                        {
                            var kinds = new SortedSet<string>(StringComparer.Ordinal);
                            for (int k = 1; k < ch.Alpha.Length; k++)
                            {
                                AlphaMap m = ch.Alpha[k];
                                if (m.Storage == AlphaStorage.Compressed) kinds.Add("rle");
                                else
                                {
                                    kinds.Add("raw" + m.StoredBytes);
                                    Inc(stats["raw_alpha_spacing"], m.StoredBytes.ToString());
                                }
                            }
                            string kind = kinds.Count > 0 ? string.Join("+", kinds) : "none";
                            verdict = kind + ": " + (kind.Contains("rle") ? "RLE ends where the next layer starts, raw spacing 2048/4096"
                                                                          : "raw spacing 2048/4096, first offset 0");
                        }
                        else verdict = "C# DECODE FAILED";
                        Inc(alpha, verdict);
                        foreach (TerrainLayer L in ch.Layers) Inc(stats["mcly_flags"], L.Flags.ToString());
                    }
                    to["tex0"] = new Dictionary<string, object> { { "top", TopList(tex.Chunks) }, { "subchunk_sets", subSets }, { "alpha", alpha },
                                                                  { "layers_per_chunk", perChunk },
                                                                  { "textures", new Dictionary<string, int> { { "mdid", tex.DiffuseTextureIds.Length }, { "mhid", tex.HeightTextureIds.Length },
                                                                                                             { "mtxp", tex.MtxpRaw != null ? tex.MtxpRaw.Length / 16 : 0 } } } };
                }
                AdtObj0 o0 = t.Parsed.Obj0.Value;
                if (o0 != null)
                {
                    var subSets = new SortedDictionary<string, int>(StringComparer.Ordinal);
                    byte[] data = t.Bytes["obj0"];
                    foreach (WorldChunk k in WorldChunkWalker.Walk(data, 0, data.Length, "obj0", "file").Where(k => k.Tag == "MCNK"))
                        Inc(subSets, GroupedTags(WorldChunkWalker.Walk(data, k.DataOffset, k.DataOffset + k.Size, "obj0", "MCNK").ToArray()));
                    foreach (DoodadPlacement d in o0.Doodads) Inc(stats["mddf_flags"], d.Flags.ToString());
                    foreach (WmoPlacement p in o0.Wmos) Inc(stats["modf_flags"], p.Flags.ToString());
                    to["obj0"] = new Dictionary<string, object> { { "top", TopList(o0.Chunks) }, { "subchunk_sets", subSets }, { "mddf", o0.Doodads.Length },
                                                                  { "modf", o0.Wmos.Length } };
                }
                if (t.Parsed.Obj1.Value != null) to["obj1"] = new Dictionary<string, object> { { "top", TopList(t.Parsed.Obj1.Value.Chunks) } };
                if (t.Parsed.Lod.Value != null) to["lod"] = new Dictionary<string, object> { { "top", TopList(t.Parsed.Lod.Value.Chunks) } };
                tiles[t.X + "_" + t.Y] = to;
            }
            int bad = stats["root_header_index_ok"].Where(kv => kv.Key != "True").Sum(kv => kv.Value);
            Verdict(bad == 0, w.Client + " ADT: root MCNK index_x/index_y = chunk order (test_corpus_rules)", bad + " mismatches");
            var so = new Dictionary<string, object>();
            foreach (var kv in stats) so[kv.Key] = kv.Value;
            return new Dictionary<string, object> { { "tiles", tiles }, { "stats", so } };
        }

        static void FitInc(SortedDictionary<string, int> fit, string name, double ex, double ey, double px, double py)
        {
            Inc(fit, name + (Math.Abs(ex - px) < 0.01 && Math.Abs(ey - py) < 0.01 ? " fits" : " does not fit"));
        }

        static List<object[]> TopList(WorldChunk[] chunks)
        {
            var r = new List<object[]>();
            foreach (WorldChunk k in chunks)
            {
                if (r.Count > 0 && (string)r[r.Count - 1][0] == k.Tag) r[r.Count - 1][1] = (int)r[r.Count - 1][1] + 1;
                else r.Add(new object[] { k.Tag, 1 });
            }
            return r;
        }

        // ================================================================================== coords.py verdicts

        static readonly string[] SymNames = { "identity", "transpose", "mirror rows", "mirror columns", "rotate 180",
                                              "transpose + mirror rows", "transpose + mirror columns", "anti-transpose" };

        static void Sym(int s, int n, int r, int c, out int rr, out int cc)
        {
            int m = n - 1;
            switch (s)
            {
                case 0: rr = r; cc = c; break;
                case 1: rr = c; cc = r; break;
                case 2: rr = m - r; cc = c; break;
                case 3: rr = r; cc = m - c; break;
                case 4: rr = m - r; cc = m - c; break;
                case 5: rr = m - c; cc = r; break;
                case 6: rr = c; cc = m - r; break;
                default: rr = m - c; cc = m - r; break;
            }
        }

        static double Median(List<double> v) { v.Sort(); return v[v.Count / 2]; }

        static Dictionary<string, object> Coords(Window w)
        {
            var result = new Dictionary<string, object> { { "chunks", w.Chunks.Count } };
            var withHeights = w.Chunks.Values.Where(c => c.Heights != null).ToList();

            // 1. outer vertex order: every symmetry of the 9x9 grid; shared points must agree.
            var vres = new Dictionary<string, object>();
            for (int s = 0; s < 8; s++)
            {
                var pts = new Dictionary<(int, int), (double min, double max, int n, int tile0, bool multiTile)>();
                foreach (Chunk ch in withHeights)
                    for (int r = 0; r < 9; r++)
                        for (int c = 0; c < 9; c++)
                        {
                            int rr, cc;
                            Sym(s, 9, r, c, out rr, out cc);
                            double z = (double)ch.Root.StoredPosition.C + ch.Heights[rr * 17 + cc];
                            var key = (ch.Ty * 128 + ch.Iy * 8 + r, ch.Tx * 128 + ch.Ix * 8 + c);
                            int tile = ch.Tx * 64 + ch.Ty;
                            (double min, double max, int n, int tile0, bool multiTile) e;
                            if (pts.TryGetValue(key, out e))
                                pts[key] = (Math.Min(e.min, z), Math.Max(e.max, z), e.n + 1, e.tile0, e.multiTile || e.tile0 != tile);
                            else
                                pts[key] = (z, z, 1, tile, false);
                        }
                int shared = 0, differing = 0, seamPts = 0, seamDiff = 0;
                double maxd = 0;
                foreach (var e in pts.Values)
                {
                    if (e.n < 2) continue;
                    shared++;
                    double d = e.max - e.min;
                    maxd = Math.Max(maxd, d);
                    if (d > 0.001) differing++;
                    if (e.multiTile) { seamPts++; if (d > 0.001) seamDiff++; }
                }
                vres[SymNames[s]] = new Dictionary<string, object> { { "shared_points", shared }, { "differing_over_1mm", differing }, { "max_difference", maxd },
                                                                      { "tile_seam_points", seamPts }, { "tile_seam_differing_over_1mm", seamDiff } };
            }
            result["outer_vertex_order"] = vres;

            // seam x/y: stored position vs the index formula, and float32 seams built from stored positions.
            double off = 0;
            int exact = 0, seams = 0, seamBit = 0;
            foreach (Chunk ch in w.Chunks.Values)
            {
                double ex = (32 - ch.Ty) * T - ch.Iy * C, ey = (32 - ch.Tx) * T - ch.Ix * C;
                off = Math.Max(off, Math.Max(Math.Abs(ch.Root.StoredPosition.A - ex), Math.Abs(ch.Root.StoredPosition.B - ey)));
                exact += (ch.Root.StoredPosition.A == (float)ex ? 1 : 0) + (ch.Root.StoredPosition.B == (float)ey ? 1 : 0);
                Chunk nb;
                if (w.Chunks.TryGetValue((ch.Tx, ch.Ty, ch.Ix, ch.Iy + 1), out nb))
                {
                    seams++;
                    float a = ch.Root.StoredPosition.A, step = (float)(8 * U);
                    if ((float)(a - step) == nb.Root.StoredPosition.A) seamBit++;
                }
            }
            result["seam_xy"] = new Dictionary<string, object> { { "stored_position_vs_formula_max_yd", off }, { "stored_values_equal_float32_of_formula", exact },
                                                                 { "stored_values", 2 * w.Chunks.Count }, { "seams_tested", seams },
                                                                 { "seams_bit_equal_when_built_from_stored_position_in_float32", seamBit } };

            // 2. inner vertices against the mean of their cell's corners.
            var ires = new Dictionary<string, object>();
            for (int s = 0; s < 8; s++)
            {
                var d = new List<double>();
                foreach (Chunk ch in withHeights)
                {
                    float[] hv = ch.Heights;
                    for (int i = 0; i < 8; i++)
                        for (int j = 0; j < 8; j++)
                        {
                            int ii, jj;
                            Sym(s, 8, i, j, out ii, out jj);
                            double mean = ((((double)hv[i * 17 + j] + hv[i * 17 + j + 1]) + hv[(i + 1) * 17 + j]) + hv[(i + 1) * 17 + j + 1]) / 4.0;
                            d.Add(Math.Abs(hv[ii * 17 + 9 + jj] - mean));
                        }
                }
                double sum = d.Sum();
                ires[SymNames[s]] = new Dictionary<string, object> { { "median_yd", Median(d) }, { "mean_yd", sum / d.Count } };
            }
            result["inner_vertex_index"] = ires;

            // 3. placement height on the exact fan surface (doodads once per uniqueId).
            var seen = new HashSet<uint>();
            int total = 0, inside = 0;
            var signed = new List<double>();
            foreach (Tile t in w.Tiles)
            {
                if (t.Parsed.Obj0.Value == null) continue;
                foreach (DoodadPlacement dd in t.Parsed.Obj0.Value.Doodads)
                {
                    if (!seen.Add(dd.UniqueId)) continue;
                    WorldPoint p = PlacementTransform.ToWorld(dd.Position);
                    total++;
                    double? g = Ground(w, p.X, p.Y);
                    if (g.HasValue) { inside++; signed.Add(p.Z - g.Value); }
                }
            }
            var absd = signed.Select(Math.Abs).ToList();
            absd.Sort();
            signed.Sort();
            result["placement"] = new Dictionary<string, object>
            {
                { "unique_doodads", total }, { "over_terrain_in_corpus", inside }, { "median_signed_yd", signed[signed.Count / 2] },
                { "median_abs_yd", absd[absd.Count / 2] }, { "within_0_1_yd", absd.Count(v => v <= 0.1) }, { "within_2_yd", absd.Count(v => v <= 2.0) },
                { "above", signed.Count(v => v > 0.01) }, { "below", signed.Count(v => v < -0.01) },
            };

            // 4. hole bits: 8 symmetries + MSB-first, against the same tile's MODF world boxes, and continuity.
            var boxes = new Dictionary<(int, int), List<double[]>>();
            foreach (Tile t in w.Tiles)
            {
                var list = new List<double[]>();
                if (t.Parsed.Obj0.Value != null)
                    foreach (WmoPlacement p in t.Parsed.Obj0.Value.Wmos)
                    {
                        WorldPoint mn, mx;
                        PlacementTransform.ExtentsToWorld(p.ExtentsMin, p.ExtentsMax, out mn, out mx);
                        list.Add(new[] { mn.X, mx.X, mn.Y, mx.Y });
                    }
                boxes[(t.X, t.Y)] = list;
            }
            Func<int, int, double, double, bool> inBox = (tx, ty, x, y) =>
            {
                List<double[]> l;
                return boxes.TryGetValue((tx, ty), out l) && l.Any(b => b[0] <= x && x <= b[1] && b[2] <= y && y <= b[3]);
            };
            var holeChunks = w.Chunks.Values.Where(c => (c.Root.Flags & 0x10000) != 0 && c.Root.Holes.HighResolutionBits != 0).ToList();
            var discriminating = new HashSet<Chunk>();
            foreach (Chunk ch in holeChunks)
            {
                bool all = true;
                for (int r = 0; r < 8 && all; r++)
                    for (int c = 0; c < 8 && all; c++)
                        all &= inBox(ch.Tx, ch.Ty, ch.Root.StoredPosition.A - (r + 0.5) * U, ch.Root.StoredPosition.B - (c + 0.5) * U);
                if (!all) discriminating.Add(ch);
            }
            var hres = new Dictionary<string, object>();
            for (int s = 0; s < 9; s++)
            {
                int cells = 0, cellsIn = 0, dcells = 0, dcellsIn = 0;
                var holeSet = new HashSet<(int, int)>();
                foreach (Chunk ch in holeChunks)
                {
                    ulong word = ch.Root.Holes.HighResolutionBits;
                    for (int r = 0; r < 8; r++)
                        for (int c = 0; c < 8; c++)
                        {
                            bool set;
                            if (s == 8) set = ((word >> (63 - (r * 8 + c))) & 1) != 0;
                            else { int rr, cc; Sym(s, 8, r, c, out rr, out cc); set = ((word >> (rr * 8 + cc)) & 1) != 0; }
                            if (!set) continue;
                            bool ok = inBox(ch.Tx, ch.Ty, ch.Root.StoredPosition.A - (r + 0.5) * U, ch.Root.StoredPosition.B - (c + 0.5) * U);
                            cells++;
                            if (ok) cellsIn++;
                            if (discriminating.Contains(ch)) { dcells++; if (ok) dcellsIn++; }
                            holeSet.Add((ch.Ty * 128 + ch.Iy * 8 + r, ch.Tx * 128 + ch.Ix * 8 + c));
                        }
                }
                int cross = 0;
                foreach (var (R, Cc) in holeSet)
                {
                    if (holeSet.Contains((R + 1, Cc)) && R % 8 == 7) cross++;
                    if (holeSet.Contains((R, Cc + 1)) && Cc % 8 == 7) cross++;
                }
                hres[s == 8 ? "whole word MSB first" : SymNames[s]] = new Dictionary<string, object>
                {
                    { "hole_cells", cells }, { "inside_wmo_box", cellsIn }, { "cells_in_discriminating_chunks", dcells },
                    { "discriminating_cells_inside", dcellsIn }, { "cross_chunk_hole_adjacencies", cross },
                };
            }
            int blocky = holeChunks.Count(ch =>
            {
                ulong m = ch.Root.Holes.HighResolutionBits;
                for (int r = 0; r < 8; r++)
                    for (int c = 0; c < 8; c++)
                        if (((m >> (r * 8 + c)) & 1) != ((m >> ((r - r % 2) * 8 + (c - c % 2))) & 1)) return false;
                return true;
            });
            result["holes"] = new Dictionary<string, object> { { "chunks_with_holes", holeChunks.Count }, { "discriminating_chunks", discriminating.Count },
                                                              { "masks_made_of_whole_2x2_blocks", blocky }, { "orders", hres } };

            // 5. alpha orientation on both edges (same tile neighbours, same texture index); nibble order.
            var ares = new Dictionary<string, object>();
            for (int s = 0; s < 8; s++)
                ares[SymNames[s]] = new Dictionary<string, object> { { "edge across index_y (vertex rows)", EdgeStats(w, s, false, true) },
                                                                     { "edge across index_x (vertex columns)", EdgeStats(w, s, false, false) } };
            result["alpha_orientation"] = ares;
            bool any4 = w.Chunks.Values.Any(c => c.Tex != null && c.Tex.Alpha != null && c.Tex.Alpha.Any(a => a != null && a.BitDepth == 4));
            if (any4)
            {
                var nib = new Dictionary<string, object>();
                foreach (bool hiFirst in new[] { false, true })
                {
                    var within = new List<double>();
                    foreach (Chunk ch in w.Chunks.Values)
                        foreach (byte[] a in AlphaArrays(w, ch, hiFirst))
                        {
                            if (a == null) continue;
                            for (int r = 0; r < 64; r++)
                                for (int c = 1; c < 63; c += 2)
                                    within.Add(Math.Abs(a[r * 64 + c] - a[r * 64 + c + 1]));
                        }
                    nib[hiFirst ? "high nibble first" : "low nibble first"] = new Dictionary<string, object>
                    {
                        { "column_edge", EdgeStats(w, 0, hiFirst, false) }, { "across_byte_boundaries_mean", Math.Round(within.Sum() / within.Count, 3) },
                    };
                }
                result["nibble_order_4bit"] = nib;
            }

            // test_corpus_rules verdicts
            var ov = (Dictionary<string, object>)vres["identity"];
            Verdict((int)ov["differing_over_1mm"] == 0 && SymNames.Skip(1).All(n => (int)((Dictionary<string, object>)vres[n])["differing_over_1mm"] > 0),
                    w.Client + " coords: outer MCVT[17r+c] is the only symmetry with equal seam heights");
            double innerId = (double)((Dictionary<string, object>)ires["identity"])["median_yd"];
            Verdict(SymNames.Skip(1).All(n => (double)((Dictionary<string, object>)ires[n])["median_yd"] > 5 * innerId),
                    w.Client + " coords: inner MCVT[17r+9+c] at least 5x closer to its corners than any other symmetry", "median " + innerId.ToString("F4"));
            var sx = (Dictionary<string, object>)result["seam_xy"];
            Verdict((double)sx["stored_position_vs_formula_max_yd"] < 0.002 && (int)sx["seams_bit_equal_when_built_from_stored_position_in_float32"] == 0,
                    w.Client + " coords: stored positions within 0.002 yd of the formula, never seam-consistent in float32");
            var pl = (Dictionary<string, object>)result["placement"];
            Verdict((int)pl["over_terrain_in_corpus"] > 0.98 * (int)pl["unique_doodads"] && (double)pl["median_abs_yd"] < 0.05,
                    w.Client + " coords: doodads on the exact fan surface", "median |dz| " + ((double)pl["median_abs_yd"]).ToString("F4"));
            var hid = (Dictionary<string, object>)hres["identity"];
            bool holesOk = (int)hid["cells_in_discriminating_chunks"] > 0 && (int)hid["discriminating_cells_inside"] == (int)hid["cells_in_discriminating_chunks"];
            foreach (var kv in hres)
                if (kv.Key != "identity")
                {
                    var o = (Dictionary<string, object>)kv.Value;
                    holesOk &= (int)o["discriminating_cells_inside"] < (int)hid["discriminating_cells_inside"] &&
                               (int)o["cross_chunk_hole_adjacencies"] < (int)hid["cross_chunk_hole_adjacencies"];
                }
            // "Whole word MSB first" equals "rotate 180" (bit 63 - (8r + c) = 8(7 - r) + (7 - c)): the list above
            // holds 8 distinct orders (kept for the comparison with coords.py). Readings that are not symmetries
            // of the cell grid: the u32 halves swapped, the nibbles of each byte swapped, the u64 read at +16/+24.
            var alts = new List<KeyValuePair<string, Func<Chunk, ulong>>>
            {
                new KeyValuePair<string, Func<Chunk, ulong>>("u32 halves swapped", ch => (ch.Root.Holes.HighResolutionBits << 32) | (ch.Root.Holes.HighResolutionBits >> 32)),
                new KeyValuePair<string, Func<Chunk, ulong>>("nibbles swapped", ch =>
                {
                    ulong v = ch.Root.Holes.HighResolutionBits;
                    return ((v & 0x0F0F0F0F0F0F0F0FUL) << 4) | ((v >> 4) & 0x0F0F0F0F0F0F0F0FUL);
                }),
                new KeyValuePair<string, Func<Chunk, ulong>>("u64 at +16", ch => BitConverter.ToUInt64(ch.Root.RawHeader, 16)),
                new KeyValuePair<string, Func<Chunk, ulong>>("u64 at +24", ch => BitConverter.ToUInt64(ch.Root.RawHeader, 24)),
            };
            var altRes = new Dictionary<string, object>();
            int idCross = (int)hid["cross_chunk_hole_adjacencies"];
            foreach (var a in alts)
            {
                int dc = 0, dcIn = 0;
                var set = new HashSet<(int, int)>();
                foreach (Chunk ch in holeChunks)
                {
                    ulong word = a.Value(ch);
                    for (int r = 0; r < 8; r++)
                        for (int c = 0; c < 8; c++)
                        {
                            if (((word >> (r * 8 + c)) & 1) == 0) continue;
                            if (discriminating.Contains(ch))
                            {
                                dc++;
                                if (inBox(ch.Tx, ch.Ty, ch.Root.StoredPosition.A - (r + 0.5) * U, ch.Root.StoredPosition.B - (c + 0.5) * U)) dcIn++;
                            }
                            set.Add((ch.Ty * 128 + ch.Iy * 8 + r, ch.Tx * 128 + ch.Ix * 8 + c));
                        }
                }
                int cross = 0;
                foreach (var (R, Cc) in set)
                {
                    if (set.Contains((R + 1, Cc)) && R % 8 == 7) cross++;
                    if (set.Contains((R, Cc + 1)) && Cc % 8 == 7) cross++;
                }
                double frac = dc > 0 ? (double)dcIn / dc : 0.0;
                altRes[a.Key] = new Dictionary<string, object> { { "cells_in_discriminating_chunks", dc }, { "discriminating_cells_inside", dcIn },
                                                                 { "fraction_inside", Math.Round(frac, 4) }, { "cross_chunk_hole_adjacencies", cross } };
                holesOk &= frac < 1.0 && cross < idCross;
            }
            result["holes_non_symmetry_readings"] = altRes;
            Verdict(holesOk, w.Client + " coords: hole bit row*8+col LSB-first wins both hole tests against the 7 other distinct orders and 4 non-symmetric readings");
            Func<string, double> score = n =>
            {
                var o = (Dictionary<string, object>)ares[n];
                return (double)((Dictionary<string, object>)o["edge across index_y (vertex rows)"])["mean"] +
                       (double)((Dictionary<string, object>)o["edge across index_x (vertex columns)"])["mean"];
            };
            Verdict(SymNames.Skip(1).All(n => score(n) > 1.5 * score("identity")),
                    w.Client + " coords: alpha row*64+col continues across both chunk edges, no other orientation does");
            if (any4)
            {
                var lo = (Dictionary<string, object>)((Dictionary<string, object>)result["nibble_order_4bit"])["low nibble first"];
                var hi = (Dictionary<string, object>)((Dictionary<string, object>)result["nibble_order_4bit"])["high nibble first"];
                Verdict((double)((Dictionary<string, object>)lo["column_edge"])["mean"] < (double)((Dictionary<string, object>)hi["column_edge"])["mean"] &&
                        (double)lo["across_byte_boundaries_mean"] < (double)hi["across_byte_boundaries_mean"],
                        w.Client + " coords: 4-bit maps are low nibble first (both nibble tests)");
            }
            return result;
        }

        /// <summary>The alpha arrays of one chunk's layers after the first: low-nibble-first from the
        /// parser, or (hiFirst) the alternative decode of the same 4-bit bytes for the nibble test.</summary>
        static IEnumerable<byte[]> AlphaArrays(Window w, Chunk ch, bool hiFirst)
        {
            if (ch.Tex == null || ch.Tex.Alpha == null) yield break;
            for (int k = 1; k < ch.Tex.Alpha.Length; k++)
            {
                AlphaMap a = ch.Tex.Alpha[k];
                if (a == null) { yield return null; continue; }
                if (!hiFirst || a.BitDepth != 4) { yield return a.Samples; continue; }
                var swapped = new byte[4096];
                for (int i = 0; i < 2048; i++) { swapped[2 * i] = a.Samples[2 * i + 1]; swapped[2 * i + 1] = a.Samples[2 * i]; }
                yield return swapped;
            }
        }

        static Dictionary<string, object> EdgeStats(Window w, int s, bool hiFirst, bool rows)
        {
            var diffs = new List<int>();
            foreach (Chunk ch in w.Chunks.Values)
            {
                Chunk nb;
                if (!w.Chunks.TryGetValue(rows ? (ch.Tx, ch.Ty, ch.Ix, ch.Iy + 1) : (ch.Tx, ch.Ty, ch.Ix + 1, ch.Iy), out nb)) continue;
                var here = ByTexture(w, ch, hiFirst);
                var there = ByTexture(w, nb, hiFirst);
                foreach (var kv in here)
                {
                    byte[] b;
                    if (!there.TryGetValue(kv.Key, out b)) continue;
                    byte[] a = kv.Value;
                    for (int k = 0; k < 64; k++)
                    {
                        int r1, c1, r2, c2;
                        if (rows) { Sym(s, 64, 63, k, out r1, out c1); Sym(s, 64, 0, k, out r2, out c2); }
                        else { Sym(s, 64, k, 63, out r1, out c1); Sym(s, 64, k, 0, out r2, out c2); }
                        diffs.Add(Math.Abs(a[r1 * 64 + c1] - b[r2 * 64 + c2]));
                    }
                }
            }
            diffs.Sort();
            return new Dictionary<string, object>
            {
                { "samples", diffs.Count }, { "median", diffs.Count > 0 ? (object)diffs[diffs.Count / 2] : null },
                { "mean", diffs.Count > 0 ? (object)Math.Round(diffs.Sum(v => (double)v) / diffs.Count, 3) : null },
            };
        }

        static Dictionary<uint, byte[]> ByTexture(Window w, Chunk ch, bool hiFirst)
        {
            var r = new Dictionary<uint, byte[]>();
            if (ch.Tex == null || ch.Tex.Alpha == null) return r;
            var arrays = AlphaArrays(w, ch, hiFirst).ToList();
            for (int k = 1; k < ch.Tex.Layers.Length; k++)
                if (arrays[k - 1] != null) r[ch.Tex.Layers[k].TextureIndex] = arrays[k - 1];
            return r;
        }

        /// <summary>Exact terrain height: four triangles per cell, fanned to the inner vertex (coords.py ground()).</summary>
        static double? Ground(Window w, double x, double y)
        {
            double gr = (32 * T - x) / U, gc = (32 * T - y) / U;
            int R = (int)Math.Floor(gr), Cc = (int)Math.Floor(gc);
            double fr = gr - R, fc = gc - Cc;
            int ty = FloorDiv(R, 128), iy = FloorMod(R, 128) / 8, r = FloorMod(R, 8);
            int tx = FloorDiv(Cc, 128), ix = FloorMod(Cc, 128) / 8, c = FloorMod(Cc, 8);
            Chunk ch;
            if (!w.Chunks.TryGetValue((tx, ty, ix, iy), out ch) || ch.Heights == null) return null;
            float[] hv = ch.Heights;
            double z0 = ch.Root.StoredPosition.C;
            double a = z0 + hv[r * 17 + c], b = z0 + hv[r * 17 + c + 1];
            double cc = z0 + hv[(r + 1) * 17 + c], d = z0 + hv[(r + 1) * 17 + c + 1];
            double m = z0 + hv[r * 17 + 9 + c];
            double u = fr - 0.5, v = fc - 0.5;
            if (Math.Abs(u) >= Math.Abs(v))
            {
                double p = u < 0 ? a : cc, q = u < 0 ? b : d;
                double t = u != 0 ? v / (2 * Math.Abs(u)) + 0.5 : 0.5;
                double edge = p + (q - p) * t;
                return m + (edge - m) * (2 * Math.Abs(u));
            }
            else
            {
                double p = v < 0 ? a : b, q = v < 0 ? cc : d;   // coords.py: (a, cc) if v < 0 else (b, d)
                double t = v != 0 ? u / (2 * Math.Abs(v)) + 0.5 : 0.5;
                double edge = p + (q - p) * t;
                return m + (edge - m) * (2 * Math.Abs(v));
            }
        }

        static int FloorDiv(int a, int b) { return (int)Math.Floor((double)a / b); }
        static int FloorMod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }

        // ================================================================================== seams, dedup, liquids, alpha edges

        /// <summary>Spec report: tile pairs, shared edges, height and position discrepancies, computed
        /// with the parser's own TerrainCoordinates (positions from both sides of every edge).</summary>
        static Dictionary<string, object> SeamReport(Window w)
        {
            var tiles = new HashSet<(int, int)>(w.Tiles.Select(t => (t.X, t.Y)));
            int tilePairs = tiles.Count(t => tiles.Contains((t.Item1 + 1, t.Item2))) + tiles.Count(t => tiles.Contains((t.Item1, t.Item2 + 1)));
            int edges = 0, crossTileEdges = 0, points = 0, heightMismatch = 0, posMismatch = 0;
            double maxH = 0, maxP = 0;
            foreach (Chunk ch in w.Chunks.Values)
            {
                if (ch.Heights == null) continue;
                foreach (bool alongRows in new[] { true, false })
                {
                    int nx = ch.Ix, ny = ch.Iy, ntx = ch.Tx, nty = ch.Ty;
                    if (alongRows) { ny++; if (ny == 16) { ny = 0; nty++; } }
                    else { nx++; if (nx == 16) { nx = 0; ntx++; } }
                    Chunk nb;
                    if (!w.Chunks.TryGetValue((ntx, nty, nx, ny), out nb) || nb.Heights == null) continue;
                    edges++;
                    if (ntx != ch.Tx || nty != ch.Ty) crossTileEdges++;
                    for (int k = 0; k < 9; k++)
                    {
                        int r1 = alongRows ? 8 : k, c1 = alongRows ? k : 8, r2 = alongRows ? 0 : k, c2 = alongRows ? k : 0;
                        double x1, y1, x2, y2;
                        TerrainCoordinates.OuterVertex(ch.Tx, ch.Ty, ch.Ix, ch.Iy, r1, c1, out x1, out y1);
                        TerrainCoordinates.OuterVertex(nb.Tx, nb.Ty, nb.Ix, nb.Iy, r2, c2, out x2, out y2);
                        double z1 = (double)ch.Root.StoredPosition.C + ch.Heights[TerrainVertexLayout.Outer(r1, c1)];
                        double z2 = (double)nb.Root.StoredPosition.C + nb.Heights[TerrainVertexLayout.Outer(r2, c2)];
                        points++;
                        double dp = Math.Max(Math.Abs(x1 - x2), Math.Abs(y1 - y2)), dh = Math.Abs(z1 - z2);
                        maxP = Math.Max(maxP, dp);
                        maxH = Math.Max(maxH, dh);
                        if (dp != 0) posMismatch++;
                        if (dh > 0.001) heightMismatch++;
                    }
                }
            }
            // Heights at shared points are evidence from the bytes; bit-equal positions are a property of
            // the integer-index formula (both sides evaluate the same expression), checked here as a
            // regression guard, and over a whole map row in WorldParserTests.
            Verdict(heightMismatch == 0 && edges > 0, w.Client + " seams: every shared edge point has equal heights (<= 1 mm); positions bit-equal by construction (" +
                    posMismatch + " differ)",
                    string.Format("{0} tile pairs, {1} chunk edges ({2} across tiles), {3} points, max |dz| {4:E2}, max |dxy| {5}", tilePairs, edges,
                                  crossTileEdges, points, maxH, maxP));
            return new Dictionary<string, object> { { "tile_pairs", tilePairs }, { "chunk_edges", edges }, { "cross_tile_edges", crossTileEdges },
                                                    { "points", points }, { "max_height_difference", maxH }, { "max_position_difference", maxP },
                                                    { "height_mismatches_over_1mm", heightMismatch }, { "position_mismatches", posMismatch } };
        }

        /// <summary>Records with the same uniqueId across tiles: kept (the parser drops nothing); are the
        /// copies identical within a client's window?</summary>
        static Dictionary<string, object> DedupReport(Window w)
        {
            var doodads = new Dictionary<uint, List<DoodadPlacement>>();
            var wmos = new Dictionary<uint, List<WmoPlacement>>();
            int dRecords = 0, wRecords = 0;
            foreach (Tile t in w.Tiles)
            {
                if (t.Parsed.Obj0.Value == null) continue;
                foreach (DoodadPlacement d in t.Parsed.Obj0.Value.Doodads) { dRecords++; Add(doodads, d.UniqueId, d); }
                foreach (WmoPlacement p in t.Parsed.Obj0.Value.Wmos) { wRecords++; Add(wmos, p.UniqueId, p); }
            }
            int dDiffer = doodads.Values.Count(l => l.Any(x => !x.SamePlacementAs(l[0]) || x.Flags != l[0].Flags));
            int wDiffer = wmos.Values.Count(l => l.Any(x => !x.SamePlacementAs(l[0])));
            int dMulti = doodads.Values.Count(l => l.Count > 1), wMulti = wmos.Values.Count(l => l.Count > 1);
            int sameTileRepeat = doodads.Values.Count(l => l.GroupBy(x => (x.Source.TileX, x.Source.TileY)).Any(g => g.Count() > 1)) +
                                 wmos.Values.Count(l => l.GroupBy(x => (x.Source.TileX, x.Source.TileY)).Any(g => g.Count() > 1));
            Verdict(dDiffer == 0 && wDiffer == 0, w.Client + " dedup: every uniqueId's copies are identical records",
                    string.Format("MDDF {0} records / {1} ids ({2} repeated); MODF {3} / {4} ({5} repeated)", dRecords, doodads.Count, dMulti, wRecords, wmos.Count, wMulti));
            return new Dictionary<string, object> { { "mddf_records", dRecords }, { "mddf_unique_ids", doodads.Count }, { "mddf_ids_repeated", dMulti },
                                                    { "mddf_ids_with_differing_copies", dDiffer }, { "modf_records", wRecords }, { "modf_unique_ids", wmos.Count },
                                                    { "modf_ids_repeated", wMulti }, { "modf_ids_with_differing_copies", wDiffer },
                                                    { "ids_repeated_within_one_tile", sameTileRepeat } };
        }

        static void Add<TK, TV>(Dictionary<TK, List<TV>> d, TK k, TV v)
        {
            List<TV> l;
            if (!d.TryGetValue(k, out l)) d[k] = l = new List<TV>();
            l.Add(v);
        }

        /// <summary>Invariants of the raw MH2O instance bytes, measured (no meaning asserted).</summary>
        static Dictionary<string, object> LiquidReport(Window w)
        {
            int tiles = 0, instances = 0, fitsGrid = 0, minLeMax = 0, finite = 0;
            var statuses = new SortedDictionary<string, int>(StringComparer.Ordinal);
            foreach (Tile t in w.Tiles)
            {
                AdtRoot r = t.Parsed.Root.Value;
                if (r == null || r.Liquids == null) continue;
                tiles++;
                Inc(statuses, r.Liquids.Status.ToString());
                if (r.Liquids.Instances == null) continue;
                foreach (LiquidInstanceRaw[] list in r.Liquids.Instances)
                    foreach (LiquidInstanceRaw i in list)
                    {
                        instances++;
                        if (i.Byte12 + i.Byte14 <= 8 && i.Byte13 + i.Byte15 <= 8) fitsGrid++;
                        if (!float.IsNaN(i.Float4) && !float.IsInfinity(i.Float4) && !float.IsNaN(i.Float8) && !float.IsInfinity(i.Float8))
                        {
                            finite++;
                            if (i.Float4 <= i.Float8) minLeMax++;
                        }
                    }
            }
            return new Dictionary<string, object> { { "tiles_with_mh2o", tiles }, { "statuses", statuses }, { "instances", instances },
                                                    { "byte12_plus_byte14_and_byte13_plus_byte15_at_most_8", fitsGrid },
                                                    { "float4_float8_finite", finite }, { "float4_le_float8", minLeMax } };
        }

        /// <summary>Repeated last alpha row/column (alpha_edges.py) and the raw 8-bit layers a
        /// row-62-to-63 copy would change.</summary>
        static Dictionary<string, object> AlphaEdges(Window w)
        {
            int maps = 0, uniform = 0, nonUniform = 0, row = 0, col = 0, both = 0, raw8 = 0, raw8Diff = 0, fourBit = 0;
            foreach (Chunk ch in w.Chunks.Values)
            {
                if (ch.Tex == null || ch.Tex.Alpha == null) continue;
                for (int k = 1; k < ch.Tex.Alpha.Length; k++)
                {
                    AlphaMap m = ch.Tex.Alpha[k];
                    if (m == null) continue;
                    maps++;
                    byte[] a = m.Samples;
                    if (m.BitDepth == 4) fourBit++;
                    if (m.Storage == AlphaStorage.Uncompressed8 && a.Max() > 15)
                    {
                        raw8++;
                        bool same = true;
                        for (int c = 0; c < 64; c++) same &= a[63 * 64 + c] == a[62 * 64 + c];
                        if (!same) raw8Diff++;
                    }
                    if (a.All(v => v == a[0])) { uniform++; continue; }
                    nonUniform++;
                    bool rr = true, cc = true;
                    for (int c = 0; c < 64; c++) rr &= a[63 * 64 + c] == a[62 * 64 + c];
                    for (int r = 0; r < 64; r++) cc &= a[r * 64 + 63] == a[r * 64 + 62];
                    if (rr) row++;
                    if (cc) col++;
                    if (rr && cc) both++;
                }
            }
            return new Dictionary<string, object> { { "maps", maps }, { "four_bit_maps", fourBit }, { "uniform maps", uniform }, { "non-uniform maps", nonUniform },
                                                    { "row 63 = row 62", row }, { "column 63 = column 62", col }, { "both", both },
                                                    { "raw 8-bit maps", raw8 }, { "raw 8-bit maps with row 63 != row 62", raw8Diff } };
        }

        // ================================================================================== rotation.py / mldx.py sweeps (Retail)

        static readonly int[][] Perms = { new[] { 0, 1, 2 }, new[] { 0, 2, 1 }, new[] { 1, 0, 2 }, new[] { 1, 2, 0 }, new[] { 2, 0, 1 }, new[] { 2, 1, 0 } };
        static readonly string[] Mirrors = { "none", "mirror model x", "mirror model y", "mirror model z" };

        static double[] MirrorMatrix(int m)
        {
            var r = new[] { 1.0, 0, 0, 0, 1.0, 0, 0, 0, 1.0 };
            if (m > 0) r[4 * (m - 1)] = -1.0;
            return r;
        }

        static double[] Box(WorldFloat3 position, double[] m, double s, double[] box, bool placementFrame)
        {
            var W0 = PlacementTransform.ToWorld(position);
            var lo = new double[] { double.MaxValue, double.MaxValue, double.MaxValue };
            var hi = new double[] { double.MinValue, double.MinValue, double.MinValue };
            for (int k = 0; k < 8; k++)
            {
                double c0 = s * ((k & 4) == 0 ? box[0] : box[3]), c1 = s * ((k & 2) == 0 ? box[1] : box[4]), c2 = s * ((k & 1) == 0 ? box[2] : box[5]);
                double vx = m[0] * c0 + m[1] * c1 + m[2] * c2, vy = m[3] * c0 + m[4] * c1 + m[5] * c2, vz = m[6] * c0 + m[7] * c1 + m[8] * c2;
                double X = W0.X + vx, Y = W0.Y + vy, Z = W0.Z + vz;
                double[] q = placementFrame ? new[] { 32 * T - Y, Z, 32 * T - X } : new[] { X, Y, Z };
                for (int i = 0; i < 3; i++) { lo[i] = Math.Min(lo[i], q[i]); hi[i] = Math.Max(hi[i], q[i]); }
            }
            return new[] { lo[0], lo[1], lo[2], hi[0], hi[1], hi[2] };
        }

        static double[] ReadBox(byte[] b, int offset) { var r = new double[6]; for (int i = 0; i < 6; i++) r[i] = BitConverter.ToSingle(b, offset + 4 * i); return r; }

        static Dictionary<string, object> RotationSweep(string root, Window w)
        {
            var byUid = new Dictionary<uint, WmoPlacement>();
            var order = new List<uint>();
            int entries = 0;
            foreach (Tile t in w.Tiles)
                if (t.Parsed.Obj0.Value != null)
                    foreach (WmoPlacement p in t.Parsed.Obj0.Value.Wmos)
                    {
                        entries++;
                        if (!byUid.ContainsKey(p.UniqueId)) { byUid[p.UniqueId] = p; order.Add(p.UniqueId); }
                    }
            var boxes = new Dictionary<uint, double[]>();
            var groupUnion = new Dictionary<uint, double[]>();
            foreach (uint name in byUid.Values.Select(p => p.NameId).Distinct())
            {
                string path = Path.Combine(root, "assets", "wow", name + ".bin");
                if (!File.Exists(path)) continue;
                byte[] b = File.ReadAllBytes(path);
                WorldChunk mohd;
                List<WorldChunk> ch;
                try { ch = WorldChunkWalker.Walk(b, 0, b.Length, "wmo", "file"); } catch (WorldParseException) { continue; }
                if (WorldChunkWalker.FindSingle(ch, "MOHD", "wmo", out mohd) && mohd.Size >= 64) boxes[name] = ReadBox(b, mohd.DataOffset + 36);
                WorldChunk mogi;
                if (WorldChunkWalker.FindSingle(ch, "MOGI", "wmo", out mogi) && mogi.Size >= 32)
                {
                    // The union of the WMO's group boxes (MOGI, 32-byte records: min at +4, max at +16).
                    var u = new[] { double.MaxValue, double.MaxValue, double.MaxValue, double.MinValue, double.MinValue, double.MinValue };
                    for (int g = 0; g < mogi.Size / 32; g++)
                    {
                        double[] gb = ReadBox(b, mogi.DataOffset + 32 * g + 4);
                        for (int i = 0; i < 3; i++) { u[i] = Math.Min(u[i], gb[i]); u[3 + i] = Math.Max(u[3 + i], gb[3 + i]); }
                    }
                    groupUnion[name] = u;
                }
            }
            var items = order.Select(u => byUid[u]).Where(p => boxes.ContainsKey(p.NameId)).ToList();
            Func<WmoPlacement, bool> tiltedP = p => Math.Abs(p.Rotation.A) > 0.01 || Math.Abs(p.Rotation.C) > 0.01;
            var results = new List<(int idx, Dictionary<string, object> row, int within, double median)>();
            int idx = 0;
            foreach (int[] axisOf in Perms)
                foreach (int[] signs in Signs3())
                    foreach (int[] ord in Perms)
                        foreach (double yawOff in new[] { 0.0, 90.0, 180.0, 270.0 })
                            for (int mi = 0; mi < 4; mi++)
                            {
                                if (mi != 0 && yawOff != 0.0 && yawOff != 180.0) continue;
                                var errs = new List<double>();
                                int within = 0, tiltedWithin = 0;
                                foreach (WmoPlacement p in items)
                                {
                                    double[] ang = { p.Rotation.A, p.Rotation.B, p.Rotation.C };
                                    double[] m = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
                                    foreach (int k in ord)
                                    {
                                        double extra = axisOf[k] == 2 ? yawOff : 0.0;
                                        m = PlacementTransform.Mul(m, PlacementTransform.Rot(axisOf[k], signs[k] * ang[k] + extra));
                                    }
                                    m = PlacementTransform.Mul(m, MirrorMatrix(mi));
                                    double s = (p.Flags & 0x4) != 0 ? p.ScaleRaw / 1024.0 : 1.0;
                                    double[] got = Box(p.Position, m, s, boxes[p.NameId], true);
                                    double[] ext = { p.ExtentsMin.A, p.ExtentsMin.B, p.ExtentsMin.C, p.ExtentsMax.A, p.ExtentsMax.B, p.ExtentsMax.C };
                                    double e = 0;
                                    for (int i = 0; i < 6; i++) e = Math.Max(e, Math.Abs(got[i] - ext[i]));
                                    errs.Add(e);
                                    if (e <= 0.1) { within++; if (tiltedP(p)) tiltedWithin++; }
                                }
                                var sorted = errs.OrderBy(v => v).ToList();
                                var row = new Dictionary<string, object>
                                {
                                    { "angle_k_turns_about_world_axis", axisOf }, { "signs", signs }, { "order_left_to_right", ord }, { "yaw_offset", yawOff },
                                    { "mirror", Mirrors[mi] }, { "within_0_1_yard", within }, { "tilted_within_0_1_yard", tiltedWithin },
                                    { "median_error", sorted[sorted.Count / 2] },
                                };
                                results.Add((idx++, row, within, sorted[sorted.Count / 2]));
                            }
            var ranked = results.OrderBy(r => -r.within).ThenBy(r => r.median).ThenBy(r => r.idx).ToList();
            var best = ranked[0].row;
            // The audit's convention, evaluated directly with the parser's PlacementTransform.
            int conv = 0, convTilted = 0;
            var misfits = new List<object>();
            foreach (WmoPlacement p in items)
            {
                double s = (p.Flags & 0x4) != 0 ? p.ScaleRaw / 1024.0 : 1.0;
                double[] bx = boxes[p.NameId];
                WorldPoint mn, mx, emn, emx;
                PlacementTransform.PlacedBox(p.Position, p.Rotation, s, new WorldPoint(bx[0], bx[1], bx[2]), new WorldPoint(bx[3], bx[4], bx[5]), out mn, out mx);
                PlacementTransform.ExtentsToWorld(p.ExtentsMin, p.ExtentsMax, out emn, out emx);
                double e = new[] { mn.X - emn.X, mn.Y - emn.Y, mn.Z - emn.Z, mx.X - emx.X, mx.Y - emx.Y, mx.Z - emx.Z }.Max(Math.Abs);
                if (e <= 0.1) { conv++; if (tiltedP(p)) convTilted++; }
                else
                {
                    // The audit's explanation of a misfit: its root box (MOHD) disagrees with its own group boxes.
                    double eu = double.NaN;
                    double[] gu;
                    if (groupUnion.TryGetValue(p.NameId, out gu))
                    {
                        WorldPoint gmn, gmx;
                        PlacementTransform.PlacedBox(p.Position, p.Rotation, s, new WorldPoint(gu[0], gu[1], gu[2]), new WorldPoint(gu[3], gu[4], gu[5]), out gmn, out gmx);
                        eu = new[] { gmn.X - emn.X, gmn.Y - emn.Y, gmn.Z - emn.Z, gmx.X - emx.X, gmx.Y - emx.Y, gmx.Z - emx.Z }.Max(Math.Abs);
                    }
                    misfits.Add(new Dictionary<string, object> { { "unique_id", p.UniqueId }, { "wmo", p.NameId }, { "error", e }, { "error_with_group_box_union", eu } });
                }
            }
            int tilted = items.Count(tiltedP);
            Verdict((double)best["yaw_offset"] == 180.0 && (string)best["mirror"] == "none" && ((int[])best["signs"]).All(v => v == 1) && conv == (int)best["within_0_1_yard"],
                    "wow rotation: the best of 2,880 conventions is Rz(a1+180) Ry(a0) Rx(a2), and PlacementTransform reproduces it",
                    string.Format("{0}/{1} within 0.1 yd, tilted {2}/{3}; misfits {4}", conv, items.Count, convTilted, tilted,
                                  string.Join(", ", misfits.Select(m => ((Dictionary<string, object>)m)["unique_id"]))));
            return new Dictionary<string, object>
            {
                { "modf_entries", entries }, { "unique_placements", byUid.Count }, { "tested", items.Count }, { "tilted", tilted }, { "wmos", boxes.Count },
                { "candidates", results.Count }, { "best", ranked.Take(8).Select(r => r.row).ToList() },
                { "best_improper", ranked.First(r => (string)r.row["mirror"] != "none").row },
                { "best_without_yaw_offset", ranked.First(r => (string)r.row["mirror"] == "none" && (double)r.row["yaw_offset"] == 0.0).row },
                { "placement_transform", new Dictionary<string, object> { { "within_0_1_yard", conv }, { "tilted_within_0_1_yard", convTilted }, { "misfits", misfits } } },
            };
        }

        static IEnumerable<int[]> Signs3()
        {
            foreach (int a in new[] { 1, -1 }) foreach (int b in new[] { 1, -1 }) foreach (int c in new[] { 1, -1 }) yield return new[] { a, b, c };
        }

        static Dictionary<string, object> MldxSweep(string root, Window w)
        {
            int same = 0, differ = 0, missing = 0;
            var flagDiff = new SortedDictionary<string, int>(StringComparer.Ordinal);
            var records = new Dictionary<uint, (DoodadPlacement d, DoodadWorldBox box)>();
            var order = new List<uint>();
            foreach (Tile t in w.Tiles)
            {
                AdtObj0 o0 = t.Parsed.Obj0.Value;
                AdtObj1 o1 = t.Parsed.Obj1.Value;
                if (o0 == null || o1 == null || o1.DoodadBoxes == null) continue;
                var mddf = new Dictionary<uint, DoodadPlacement>();
                foreach (DoodadPlacement d in o0.Doodads) mddf[d.UniqueId] = d;
                for (int i = 0; i < o1.Doodads.Length; i++)
                {
                    DoodadPlacement d = o1.Doodads[i];
                    DoodadPlacement o;
                    if (!mddf.TryGetValue(d.UniqueId, out o)) missing++;
                    else if (d.SamePlacementAs(o)) { same++; Inc(flagDiff, string.Format("0x{0:X}->0x{1:X}", o.Flags, d.Flags)); }
                    else differ++;
                    if (!records.ContainsKey(d.UniqueId)) { records[d.UniqueId] = (d, o1.DoodadBoxes[i]); order.Add(d.UniqueId); }
                }
            }
            var m2 = new Dictionary<uint, double[]>();
            foreach (uint name in records.Values.Select(r => r.d.NameId).Distinct())
            {
                string path = Path.Combine(root, "assets", "wow", name + ".bin");
                if (!File.Exists(path)) continue;
                byte[] b = File.ReadAllBytes(path);
                int bs = b.Length >= 4 && b[0] == 'M' && b[1] == 'D' && b[2] == '2' && b[3] == '1' ? 8 : (b.Length >= 4 && b[0] == 'M' && b[1] == 'D' && b[2] == '2' && b[3] == '0' ? 0 : -1);
                if (bs < 0 || b.Length < bs + 0xA0 + 24) continue;
                m2[name] = ReadBox(b, bs + 0xA0);
            }
            var items = order.Select(u => records[u]).Where(r => m2.ContainsKey(r.d.NameId)).ToList();
            var results = new List<(int idx, Dictionary<string, object> row, int within)>();
            int idx = 0;
            foreach (int yawSign in new[] { 1, -1 })
                foreach (double yawOff in new[] { 0.0, 180.0 })
                    foreach (int s0 in new[] { 1, -1 })
                        foreach (int s2 in new[] { 1, -1 })
                            foreach (int[] axes in new[] { new[] { 1, 0 }, new[] { 0, 1 } })
                                foreach (string scaleRule in new[] { "u16/1024", "ignored" })
                                    foreach (bool placementFrame in new[] { false, true })
                                    {
                                        int within = 0, tilted = 0, tiltedWithin = 0;
                                        foreach (var it in items)
                                        {
                                            DoodadPlacement d = it.d;
                                            double[] m = PlacementTransform.Mul(PlacementTransform.Mul(PlacementTransform.Rot(2, yawSign * (double)d.Rotation.B + yawOff),
                                                                                                        PlacementTransform.Rot(axes[0], s0 * (double)d.Rotation.A)),
                                                                                PlacementTransform.Rot(axes[1], s2 * (double)d.Rotation.C));
                                            double s = scaleRule == "u16/1024" ? d.ScaleRaw / 1024.0 : 1.0;
                                            double[] got = Box(d.Position, m, s, m2[d.NameId], placementFrame);
                                            double[] want = { it.box.Min.A, it.box.Min.B, it.box.Min.C, it.box.Max.A, it.box.Max.B, it.box.Max.C };
                                            double e = 0;
                                            for (int i = 0; i < 6; i++) e = Math.Max(e, Math.Abs(got[i] - want[i]));
                                            bool ok = e <= 0.5;
                                            if (ok) within++;
                                            if (Math.Abs(d.Rotation.A) > 0.01 || Math.Abs(d.Rotation.C) > 0.01) { tilted++; if (ok) tiltedWithin++; }
                                        }
                                        var row = new Dictionary<string, object>
                                        {
                                            { "yaw", string.Format("{0:+0;-0}*a1{1}", yawSign, yawOff == 0 ? "+0" : "+180") }, { "pitch_roll_signs", new[] { s0, s2 } },
                                            { "rotation0_about", axes[0] == 1 ? "Y" : "X" }, { "rotation2_about", axes[1] == 1 ? "Y" : "X" }, { "scale", scaleRule },
                                            { "mldx_frame", placementFrame ? "placement (32T-Y, Z, 32T-X)" : "world (X, Y, Z)" }, { "doodads", items.Count },
                                            { "within_0_5_yard", within }, { "tilted", tilted }, { "tilted_within_0_5_yard", tiltedWithin },
                                        };
                                        results.Add((idx++, row, within));
                                    }
            var ranked = results.OrderBy(r => -r.within).ThenBy(r => r.idx).ToList();
            // The audit's convention through the parser's PlacementTransform.
            int conv = 0, convTilted = 0, tiltedAll = 0;
            foreach (var it in items)
            {
                double[] bx = m2[it.d.NameId];
                WorldPoint mn, mx;
                PlacementTransform.PlacedBox(it.d.Position, it.d.Rotation, it.d.Scale, new WorldPoint(bx[0], bx[1], bx[2]), new WorldPoint(bx[3], bx[4], bx[5]), out mn, out mx);
                double e = new[] { mn.X - it.box.Min.A, mn.Y - it.box.Min.B, mn.Z - it.box.Min.C, mx.X - it.box.Max.A, mx.Y - it.box.Max.B, mx.Z - it.box.Max.C }.Max(Math.Abs);
                bool tilt = Math.Abs(it.d.Rotation.A) > 0.01 || Math.Abs(it.d.Rotation.C) > 0.01;
                if (tilt) tiltedAll++;
                if (e <= 0.5) { conv++; if (tilt) convTilted++; }
            }
            // The rotation ORDER, which the mldx.py sweep does not vary: the proven Rz Ry Rx (Rx first)
            // against Rz Rx Ry (Ry first), on the doodads (many more tilted placements than WMOs).
            int alt = 0, altTilted = 0;
            foreach (var it in items)
            {
                double[] bx = m2[it.d.NameId];
                double[] m = PlacementTransform.Mul(PlacementTransform.Mul(PlacementTransform.Rot(2, it.d.Rotation.B + 180.0),
                                                                            PlacementTransform.Rot(0, it.d.Rotation.C)),
                                                    PlacementTransform.Rot(1, it.d.Rotation.A));
                double[] got = Box(it.d.Position, m, it.d.Scale, bx, false);
                double[] want = { it.box.Min.A, it.box.Min.B, it.box.Min.C, it.box.Max.A, it.box.Max.B, it.box.Max.C };
                double e = 0;
                for (int i = 0; i < 6; i++) e = Math.Max(e, Math.Abs(got[i] - want[i]));
                if (e <= 0.5) { alt++; if (Math.Abs(it.d.Rotation.A) > 0.01 || Math.Abs(it.d.Rotation.C) > 0.01) altTilted++; }
            }
            Verdict(convTilted > altTilted, "wow MLDX: rotation order Rz Ry Rx (Rx first) beats Rz Rx Ry on the tilted doodads",
                    string.Format("tilted within 0.5 yd: {0} vs {1} (all: {2} vs {3})", convTilted, altTilted, conv, alt));
            var b0 = ranked[0].row;
            Verdict(differ == 0 && (string)b0["yaw"] == "+1*a1+180" && (string)b0["scale"] == "u16/1024" && ((int[])b0["pitch_roll_signs"]).All(v => v == 1) &&
                    (int)b0["within_0_5_yard"] == conv && (int)ranked[1].row["within_0_5_yard"] < conv,
                    "wow MLDX: the best convention is +a1+180, signs (+,+), scale u16/1024, and PlacementTransform reproduces it",
                    string.Format("{0}/{1} within 0.5 yd, tilted {2}/{3}; MLDD = MDDF {4}, differ {5}", conv, items.Count, convTilted, tiltedAll, same, differ));
            return new Dictionary<string, object>
            {
                { "mldd_equals_mddf", new Dictionary<string, object> { { "same", same }, { "differ", differ }, { "not_in_mddf", missing }, { "flags_obj0_to_obj1", flagDiff } } },
                { "unique_doodads_with_mldx", records.Count }, { "with_m2_box", items.Count }, { "best", ranked.Take(6).Select(r => r.row).ToList() },
                { "best_scale_ignored", ranked.First(r => (string)r.row["scale"] == "ignored").row },
                { "best_without_yaw_offset", ranked.First(r => ((string)r.row["yaw"]).EndsWith("+0")).row },
                { "placement_transform", new Dictionary<string, object> { { "within_0_5_yard", conv }, { "tilted", tiltedAll }, { "tilted_within_0_5_yard", convTilted } } },
                { "order_rz_rx_ry", new Dictionary<string, object> { { "within_0_5_yard", alt }, { "tilted_within_0_5_yard", altTilted } } },
            };
        }

        // ================================================================================== performance

        static Dictionary<string, object> Performance(string root, string client, Window w)
        {
            var o = new Dictionary<string, object>();
            foreach (string part in PartNames)
            {
                var files = w.Tiles.Where(t => t.Bytes.ContainsKey(part)).Select(t => (t.X, t.Y, t.Bytes[part])).ToList();
                if (files.Count == 0) continue;
                long bytes = files.Sum(f => (long)f.Item3.Length);
                double best = double.MaxValue;
                long alloc = 0, retained = 0, measured = 0;
                int chunks = 0;
                for (int pass = 0; pass < 3; pass++)
                {
                    PassResult r = MeasurePass(part, files, pass == 0);
                    if (r.Ms < best) best = r.Ms;
                    alloc = r.Allocated;
                    if (pass == 0)
                    {
                        measured = r.Measured;
                        retained = r.Estimate;
                        chunks = r.Chunks;
                    }
                }
                o[part] = new Dictionary<string, object>
                {
                    { "files", files.Count }, { "bytes", bytes }, { "top_and_sub_chunks", chunks }, { "best_parse_ms", Math.Round(best, 2) },
                    { "mb_per_s", Math.Round(bytes / 1e6 / (best / 1000.0), 1) }, { "allocated_bytes_per_pass", alloc },
                    { "retained_bytes_measured", measured }, { "retained_vs_input", Math.Round((double)measured / bytes, 3) },
                    { "retained_bytes_estimate", retained },
                };
            }
            // The WDT itself.
            byte[] wdt = File.ReadAllBytes(Path.Combine(root, "wdt", client, "775971.bin"));
            var t0 = Stopwatch.StartNew();
            for (int i = 0; i < 20; i++) WdtParser.Parse(wdt);
            t0.Stop();
            o["wdt_azeroth"] = new Dictionary<string, object> { { "bytes", wdt.Length }, { "parse_ms", Math.Round(t0.Elapsed.TotalMilliseconds / 20, 3) },
                                                                { "retained_bytes_estimate", 4096 * 8 + 4096 * 32 + 64 } };
            say(client + " performance: " + string.Join("; ", o.Where(kv => kv.Value is Dictionary<string, object> d && d.ContainsKey("best_parse_ms"))
                                                                  .Select(kv => kv.Key + " " + ((Dictionary<string, object>)kv.Value)["best_parse_ms"] + " ms")));
            return o;
        }

        struct PassResult
        {
            public double Ms;
            public long Allocated, Measured, Estimate;
            public int Chunks;
        }

        /// <summary>One timed pass over a part's files. The results live only inside this method: once it
        /// returns, nothing references them, so the next measurement starts from a clean heap. (Kept out
        /// of the caller because unoptimised tier-0 code reports every local and spilled temporary as
        /// live for the whole method, which would carry one part's results into the next part's baseline.)</summary>
        [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
        static PassResult MeasurePass(string part, List<(int X, int Y, byte[] Bytes)> files, bool measureHeap)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
            long heap0 = measureHeap ? GC.GetTotalMemory(true) : 0;
            long a0 = GC.GetAllocatedBytesForCurrentThread();
            var sw = Stopwatch.StartNew();
            var keep = new List<object>(files.Count);
            foreach (var f in files)
            {
                switch (part)
                {
                    case "root": keep.Add(AdtParser.ParseRoot(f.Bytes, f.X, f.Y)); break;
                    case "tex0": keep.Add(AdtParser.ParseTex(f.Bytes)); break;
                    case "obj0": keep.Add(AdtParser.ParseObj0(f.Bytes, f.X, f.Y)); break;
                    case "obj1": keep.Add(AdtParser.ParseObj1(f.Bytes, f.X, f.Y)); break;
                    default: keep.Add(AdtParser.ParseLod(f.Bytes)); break;
                }
            }
            sw.Stop();
            var r = new PassResult { Ms = sw.Elapsed.TotalMilliseconds, Allocated = GC.GetAllocatedBytesForCurrentThread() - a0 };
            if (measureHeap)
            {
                // Retained: the GC heap growth while the results are held (the inputs were loaded before).
                r.Measured = GC.GetTotalMemory(true) - heap0;
                GC.KeepAlive(keep);
                r.Estimate = keep.Sum(Retained);
                r.Chunks = keep.Sum(k => ChunkCount(k));
            }
            return r;
        }

        /// <summary>Bytes held by a parse result's arrays (an estimate: array payloads + 24 B per object).</summary>
        static long Retained(object r)
        {
            long n = 0;
            if (r is AdtRoot root)
            {
                foreach (AdtRootChunk c in root.Mcnk)
                    n += 24 + Len(c.RawHeader) + 4L * (c.Heights != null ? c.Heights.Length : 0) + Len(c.NormalsRaw) + Len(c.NormalsTrailingRaw) + Len(c.VertexColorsRaw) + 64;
                if (root.Liquids != null && root.Liquids.Instances != null)
                    n += 256 * 12 + root.Liquids.Instances.Sum(a => 24L + 24L * a.Length);
                n += root.Chunks.Length * 24L;
            }
            else if (r is AdtTex tex)
            {
                foreach (AdtTexChunk c in tex.Mcnk)
                {
                    n += 24 + 16L * c.Layers.Length + Len(c.ShadowRaw);
                    if (c.Alpha != null) foreach (AlphaMap a in c.Alpha) if (a != null) n += 24 + a.Samples.Length;
                }
                n += 4L * (tex.DiffuseTextureIds.Length + tex.HeightTextureIds.Length) + Len(tex.MampRaw) + Len(tex.MtxpRaw) + tex.Chunks.Length * 24L;
            }
            else if (r is AdtObj0 o0)
                n += 48L * o0.Doodads.Length + 80L * o0.Wmos.Length + o0.Mcnk.Sum(c => 24L + 4L * ((c.McrdRaw?.Length ?? 0) + (c.McrwRaw?.Length ?? 0))) + o0.Chunks.Length * 24L;
            else if (r is AdtObj1 o1)
                n += 48L * o1.Doodads.Length + 28L * (o1.DoodadBoxes?.Length ?? 0) + o1.Chunks.Length * 24L;
            else if (r is AdtLod lod)
                n += lod.Chunks.Length * 24L;
            return n;
        }

        static long Len(Array a) { return a == null ? 0 : (long)Buffer.ByteLength(a); }

        static int ChunkCount(object r)
        {
            if (r is AdtRoot root) return root.Chunks.Length + root.Mcnk.Sum(c => 4 + (c.Unsupported?.Length ?? 0));
            if (r is AdtTex tex) return tex.Chunks.Length + tex.Mcnk.Sum(c => 1 + (c.Alpha != null ? c.Alpha.Count(a => a != null) : 0));
            if (r is AdtObj0 o0) return o0.Chunks.Length + o0.Doodads.Length + o0.Wmos.Length;
            if (r is AdtObj1 o1) return o1.Chunks.Length + o1.Doodads.Length;
            if (r is AdtLod lod) return lod.Chunks.Length;
            return 0;
        }

        // ================================================================================== helpers

        static void Inc(SortedDictionary<string, int> d, string k) { int v; d.TryGetValue(k, out v); d[k] = v + 1; }
    }
}
