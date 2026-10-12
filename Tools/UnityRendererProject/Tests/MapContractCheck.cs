// MapContractCheck.cs -- the world-map runtime contract at the data level (docs/world-viewer/02-map-catalog.md,
// section 12).
//
// The viewer's headless catalogue test reads chosen maps' WDTs through its map catalogue, the way a runtime will, and
// writes them to a folder as <product>_<map id>.wdt, each with <product>_<map id>.json: the SHA-256 of the bytes the
// catalogue read and the form Map.db2 states for the map (wowmodelviewer -mapcatalogtest ... -mapcatalogwdt <folder>
// <ids>). This step checks the bytes against that SHA-256, parses them with the verified WDT parser
// (Assets/Scripts/Wow/World), exactly as the player will, checks the parsed kind against the stated form (a tile grid:
// TiledTerrain, or Empty for a grid with no tile; WMO only: GlobalWmo), and writes <folder>/manifest.json: per map its
// kind, header flags, every tile with the eight FileDataIDs its MAID names, and each global world model's reference. A
// second viewer run then resolves those FileDataIDs through the client's own storage. No game install is touched here:
// the bytes come from the viewer, and the resolution goes back to it.
//
// Runs when WMV_MAP_CONTRACT_DIR names that folder. Runner-only (file IO, System.Text.Json).

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class MapContractCheck
    {
        public static int Run(string folder, Action<string> say)
        {
            int failures = 0;
            var maps = new List<Dictionary<string, object>>();
            foreach (string file in Directory.GetFiles(folder, "*.wdt").OrderBy(f => f, StringComparer.Ordinal))
            {
                string name = Path.GetFileNameWithoutExtension(file);
                int cut = name.LastIndexOf('_');
                int mapId;
                if (cut <= 0 || !int.TryParse(name.Substring(cut + 1), out mapId))
                {
                    failures++;
                    say("  FAIL  " + name + ": not named <product>_<map id>.wdt");
                    continue;
                }
                byte[] bytes = File.ReadAllBytes(file);
                var m = new Dictionary<string, object>
                {
                    { "product", name.Substring(0, cut) }, { "mapId", mapId }, { "bytes", bytes.Length },
                };
                // What the viewer said it read: these bytes, and the form Map.db2 states.
                string sidecar = Path.ChangeExtension(file, ".json");
                string statedForm = null;
                if (!File.Exists(sidecar))
                {
                    failures++;
                    say("  FAIL  " + name + ": no " + Path.GetFileName(sidecar) + " from the viewer");
                }
                else
                {
                    using (JsonDocument side = JsonDocument.Parse(File.ReadAllText(sidecar)))
                    {
                        string expected = side.RootElement.GetProperty("sha256").GetString();
                        statedForm = side.RootElement.GetProperty("statedWdtForm").GetString();
                        string actual;
                        using (SHA256 sha = SHA256.Create())
                            actual = string.Concat(sha.ComputeHash(bytes).Select(b => b.ToString("x2")));
                        m["sha256"] = actual;
                        m["statedWdtForm"] = statedForm;
                        if (!string.Equals(expected, actual, StringComparison.OrdinalIgnoreCase))
                        {
                            failures++;
                            say("  FAIL  " + name + ": SHA-256 " + actual + ", the viewer read " + expected);
                        }
                    }
                }
                try
                {
                    WdtFile wdt = WdtParser.Parse(bytes, name);
                    // The stated form against what the parser found (docs/world-viewer/02-map-catalog.md section 4).
                    bool formAgrees = statedForm == null ||
                                      (statedForm == "tile grid" && (wdt.Kind == WdtKind.TiledTerrain || wdt.Kind == WdtKind.Empty)) ||
                                      (statedForm == "WMO only" && wdt.Kind == WdtKind.GlobalWmo);
                    m["formAgrees"] = formAgrees;
                    if (!formAgrees)
                    {
                        failures++;
                        say("  FAIL  " + name + ": Map.db2 states " + statedForm + ", the parser found " + wdt.Kind);
                    }
                    m["kind"] = wdt.Kind.ToString();
                    m["mphdFlags"] = wdt.Header.Flags;
                    m["tileCount"] = wdt.TileCount;
                    m["issues"] = wdt.Issues.Select(i => i.ToString()).ToList();
                    var tiles = new List<Dictionary<string, object>>();
                    for (int y = 0; y < 64; y++)
                        for (int x = 0; x < 64; x++)
                        {
                            if (!wdt.TileExists(x, y))
                                continue;
                            WdtTileFiles files;
                            tiles.Add(new Dictionary<string, object>
                            {
                                { "x", x }, { "y", y },
                                { "ids", wdt.TryGetTileFiles(x, y, out files) ? files.FileDataIds : null },
                            });
                        }
                    m["tiles"] = tiles;
                    var wmos = new List<Dictionary<string, object>>();
                    for (int i = 0; i < wdt.GlobalWmos.Length; i++)
                    {
                        WorldFileReference r;
                        var w = new Dictionary<string, object> { { "placement", i } };
                        if (wdt.TryGetGlobalWmoReference(i, out r))
                        {
                            if (r.IsPath)
                                w["path"] = r.Path;
                            else
                                w["fileDataId"] = r.FileDataId;
                        }
                        else
                            w["unresolved"] = true;
                        wmos.Add(w);
                    }
                    m["globalWmos"] = wmos;
                    if (formAgrees)
                        say(string.Format("  PASS  {0}: {1} (stated {5}), {2} tiles, {3} global world model placement(s), {4} issue(s)",
                                          name, wdt.Kind, wdt.TileCount, wmos.Count, wdt.Issues.Count, statedForm ?? "-"));
                }
                catch (WowParseException e)
                {
                    failures++;
                    m["error"] = e.Message;
                    say("  FAIL  " + name + ": " + e.Message);
                }
                maps.Add(m);
            }
            if (maps.Count == 0)
            {
                failures++;
                say("  FAIL  no <product>_<map id>.wdt files in " + folder);
            }
            string manifest = Path.Combine(folder, "manifest.json");
            File.WriteAllText(manifest, JsonSerializer.Serialize(maps, new JsonSerializerOptions { WriteIndented = true }));
            say("MapContractCheck: " + maps.Count + " WDT(s), " + failures + " failure(s); manifest " + manifest);
            return failures;
        }
    }
}
