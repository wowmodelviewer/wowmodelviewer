// WorldModelCorpusTests.cs
//
// Stage 06 on REAL client bytes (docs/world-viewer/06-world-models-in-world.md section 2): the corpus
// Tools/world-audit/world06_corpus.py extracts into <WMV_WORLD_AUDIT_CORPUS>/world06 -- per client the WDTs of maps 0, 34
// and 429, Retail/Era/MoP/Beta tile 30_47's obj0, every world model they place and its LOD0 group files -- with a
// manifest of SHA-256s. Every file is checked against the manifest before it is used: a client that updated stops the
// test, it is never read as a regression.
//
//   1. Map 34 and map 429 (WMO-only WDTs): the global MODF placed in the Global frame (no map-centre offset). The placed
//      root box equals the record's extents; the client's own AreaTrigger positions on those maps (AreaTrigger.db2, pinned
//      below with their build) lie inside the placed group boxes -- and NOT under the Tile frame. Dire Maul: Era's record
//      carries a1 = 180 and its model file is stored turned 180 degrees; both placements put the triggers inside.
//   2. Tile 30_47: every obj0 MODF placement's root box (MOHD) and group-box union (MOGI) placed in the Tile frame against
//      the record's extents, the fit counted (the audit's 0.1 yard tolerance), misfits listed.
//   3. The floor under the map-34 trigger, and the start point inside each global world model: on a floor, under a
//      ceiling, inside a group box.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Wmv.Wow;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class WorldModelCorpusTests
    {
        static int failures;
        static Action<string> output;

        static void Check(bool condition, string name)
        {
            output((condition ? "  PASS  " : "  FAIL  ") + name);
            if (!condition) failures++;
        }

        static void Info(string line) { output("  INFO  " + line); }

        static readonly string[] Clients = { "wow", "wow_classic_era", "wow_classic", "wow_classic_beta" };

        // AreaTrigger.db2 rows on the two WMO-only maps (game world frame, x north, y west, z up), read from each client's own
        // table (builds as in the manifest). Map 34: row 503, present in Retail, Era and MoP; Beta's table has no row on map
        // 34. Map 429: rows 3190, 3191, 3193 .. 3197 and 3506, identical in every client that has them (Beta has 3190, 3191,
        // 3193 and 3506 .. 3509 only).
        static readonly Dictionary<string, double[][]> Map34Triggers = new Dictionary<string, double[][]>
        {
            { "wow", new[] { new[] { 48.09, 0.93, -13.78 } } },
            { "wow_classic_era", new[] { new[] { 39.37, 0.80, -12.79 } } },
            { "wow_classic", new[] { new[] { 39.37, 0.80, -12.79 } } },
            { "wow_classic_beta", new double[0][] },
        };
        static readonly double[][] Map429TriggersAll =
        {
            new[] { -55.97, 159.87, -3.46 }, new[] { 24.56, 159.45, -3.47 }, new[] { 255.25, -9.06, -2.59 }, new[] { 37.45, -154.82, -2.71 },
            new[] { -202.66, -314.88, -2.72 }, new[] { 4.31, -837.09, -33.04 }, new[] { 194.35, -240.82, -25.63 }, new[] { 580.75, 487.51, 29.46 },
        };
        static readonly double[][] Map429TriggersBeta =
        {
            new[] { -55.97, 159.87, -3.46 }, new[] { 24.56, 159.45, -3.47 }, new[] { 255.25, -9.06, -2.59 }, new[] { 580.75, 487.51, 29.46 },
        };

        internal sealed class ClientFiles
        {
            public string Name, Build, Folder;
            public Dictionary<uint, string> Sha = new Dictionary<uint, string>();
            public Dictionary<uint, string> Role = new Dictionary<uint, string>();

            public byte[] Read(uint fid)
            {
                string path = Path.Combine(Folder, fid + ".bin");
                string pin;
                if (!Sha.TryGetValue(fid, out pin))
                    throw new InvalidDataException(Name + ": FileDataID " + fid + " is not in the corpus manifest");
                byte[] b = File.ReadAllBytes(path);
                string h;
                using (SHA256 s = SHA256.Create())
                    h = BitConverter.ToString(s.ComputeHash(b)).Replace("-", "").ToLowerInvariant();
                if (h != pin)
                    throw new InvalidDataException(Name + ": FileDataID " + fid + " hashes to " + h + ", the manifest pins " + pin +
                                                   " (the client changed since the corpus was extracted: extract it again)");
                return b;
            }

            public bool Has(uint fid) { return Sha.ContainsKey(fid); }
        }

        internal static Dictionary<string, ClientFiles> LoadManifest(string folder)
        {
            var result = new Dictionary<string, ClientFiles>();
            using (JsonDocument doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(folder, "manifest.json"))))
                foreach (JsonProperty c in doc.RootElement.GetProperty("clients").EnumerateObject())
                {
                    var cf = new ClientFiles { Name = c.Name, Build = c.Value.GetProperty("build").GetString(), Folder = Path.Combine(folder, c.Name) };
                    foreach (JsonProperty f in c.Value.GetProperty("files").EnumerateObject())
                    {
                        uint id = uint.Parse(f.Name, CultureInfo.InvariantCulture);
                        cf.Sha[id] = f.Value.GetProperty("sha256").GetString();
                        cf.Role[id] = f.Value.GetProperty("role").GetString();
                    }
                    result[c.Name] = cf;
                }
            return result;
        }

        static WmoGroup[] Groups(ClientFiles cf, WmoRoot root)
        {
            uint[] ids = root.Lod0GroupFileDataIDs;
            var g = new WmoGroup[ids.Length];
            for (int i = 0; i < ids.Length; i++)
                if (ids[i] != 0 && cf.Has(ids[i]))
                    g[i] = WmoParser.ParseGroup(cf.Read(ids[i]), "group " + ids[i], i);
            return g;
        }

        static bool InsideAny(WorldModelGroupBox[] boxes, double x, double y, double z, double pad = 0.5)
        {
            foreach (WorldModelGroupBox b in boxes)
                if (x >= b.Min.X - pad && x <= b.Max.X + pad && y >= b.Min.Y - pad && y <= b.Max.Y + pad && z >= b.Min.Z - pad && z <= b.Max.Z + pad)
                    return true;
            return false;
        }

        static double BoxError(WorldPoint aMin, WorldPoint aMax, WorldPoint bMin, WorldPoint bMax)
        {
            double e = 0.0;
            e = Math.Max(e, Math.Abs(aMin.X - bMin.X));
            e = Math.Max(e, Math.Abs(aMin.Y - bMin.Y));
            e = Math.Max(e, Math.Abs(aMin.Z - bMin.Z));
            e = Math.Max(e, Math.Abs(aMax.X - bMax.X));
            e = Math.Max(e, Math.Abs(aMax.Y - bMax.Y));
            e = Math.Max(e, Math.Abs(aMax.Z - bMax.Z));
            return e;
        }

        static void GlobalWmoTests(ClientFiles cf, uint wdtId, int mapId, double[][] triggers, float expectYaw)
        {
            string who = cf.Name + " " + cf.Build + " map " + mapId;
            if (!cf.Has(wdtId))
            {
                Info(who + ": WDT " + wdtId + " not in this client: skipped");
                return;
            }
            WdtFile wdt = WdtParser.Parse(cf.Read(wdtId), "wdt " + wdtId);
            Check(wdt.Kind == WdtKind.GlobalWmo && wdt.GlobalWmos.Length == 1 && (wdt.Header.Flags & 1) != 0,
                  who + ": a WMO-only WDT (MPHD 0x1, one MODF, no tile)");
            List<WorldModelPlacement> plan = WorldModelPlan.Build(wdt.GlobalWmos, PlacementFrame.Global, null);
            WorldModelPlacement p = plan[0];
            Check(p.Unsupported == null && p.FileDataID != 0 && cf.Has(p.FileDataID), who + ": the record names WMO " + p.FileDataID +
                                                                                       " by FileDataID, in the corpus");
            Check(Math.Abs(p.Record.Rotation.B - expectYaw) < 1e-3 && Math.Abs(p.Record.Rotation.A) < 1e-3 && Math.Abs(p.Record.Rotation.C) < 1e-3,
                  who + ": the record's rotation is (0, " + expectYaw + ", 0)");
            Check(p.Position.X == 0.0 && p.Position.Y == 0.0 && p.Position.Z == 0.0, who + ": position (0, 0, 0): the world origin in the Global frame");
            WmoRoot root = WmoParser.ParseRoot(cf.Read(p.FileDataID), "root " + p.FileDataID);
            WorldPoint rmin, rmax;
            WorldModelPlan.RootBox(root, p, out rmin, out rmax);
            double err = BoxError(rmin, rmax, p.ExtentsMin, p.ExtentsMax);
            Check(err < 0.01, string.Format(CultureInfo.InvariantCulture, "{0}: the placed root box equals the record's extents (largest difference {1:0.####} yd)", who, err));
            WorldModelGroupBox[] boxes = WorldModelPlan.GroupBoxes(root, p);
            int inside = 0;
            foreach (double[] t in triggers)
                if (InsideAny(boxes, t[0], t[1], t[2]))
                    inside++;
            Check(inside == triggers.Length, string.Format("{0}: {1} of {2} AreaTrigger positions inside a placed group box (Global frame)", who, inside, triggers.Length));
            // The Tile frame (the map-centre offset) puts the model some 17,000 yards away: no trigger inside.
            WorldModelPlacement tileFrame = WorldModelPlan.Build(wdt.GlobalWmos, PlacementFrame.Tile, null)[0];
            WorldModelGroupBox[] far = WorldModelPlan.GroupBoxes(root, tileFrame);
            int insideFar = 0;
            foreach (double[] t in triggers)
                if (InsideAny(far, t[0], t[1], t[2]))
                    insideFar++;
            Check(triggers.Length == 0 || insideFar == 0, string.Format("{0}: under the Tile frame {1} of {2} trigger positions would be inside (none expected)", who, insideFar, triggers.Length));
            // Without the 180-degree yaw of the rule (R = Rz(a1) instead of Rz(a1 + 180)): fewer inside, where the triggers can tell.
            WmoPlacement noYaw = wdt.GlobalWmos[0];
            noYaw.Rotation = new WorldFloat3(noYaw.Rotation.A, noYaw.Rotation.B + 180f, noYaw.Rotation.C);
            WorldModelGroupBox[] turned = WorldModelPlan.GroupBoxes(root, WorldModelPlan.Build(new[] { noYaw }, PlacementFrame.Global, null)[0]);
            int insideTurned = 0;
            foreach (double[] t in triggers)
                if (InsideAny(turned, t[0], t[1], t[2]))
                    insideTurned++;
            Info(string.Format("{0}: turned 180 degrees from the rule, {1} of {2} trigger positions would be inside", who, insideTurned, triggers.Length));
            if (triggers.Length >= 4)
                Check(insideTurned < triggers.Length, who + ": the rule's yaw is needed (turned 180 degrees, fewer triggers inside)");

            // The geometry: the floor under each trigger, and the start point.
            WmoGroup[] groups = Groups(cf, root);
            int missing = 0;
            foreach (WmoGroup g in groups) if (g == null) missing++;
            Check(missing == 0, who + ": all " + groups.Length + " LOD0 group files parsed");
            var geo = new WorldModelGeometry(p, root, groups);
            int floors = 0;
            foreach (double[] t in triggers)
            {
                double fz;
                int gi;
                if (geo.FloorBelow(t[0], t[1], t[2] + 1.0, out fz, out gi) && fz <= t[2] + 1.0 && t[2] - fz < 12.0)
                    floors++;
            }
            Check(floors == triggers.Length, string.Format("{0}: a floor under {1} of {2} trigger positions, within 12 yards below", who, floors, triggers.Length));
            // Where the camera stands at each trigger's (x, y) when no height is given: a floor under it (covered, inside).
            int stands = 0, covered = 0;
            foreach (double[] t in triggers)
            {
                double sz, sf;
                string sr;
                if (geo.StandOn(t[0], t[1], 2.0, out sz, out sf, out sr))
                {
                    stands++;
                    if (sr.Contains("covered floor")) covered++;
                    Info(string.Format(CultureInfo.InvariantCulture, "{0}: at ({1:0.##}, {2:0.##}) StandOn z {3:0.##} (trigger z {4:0.##}): {5}", who, t[0], t[1], sz, t[2], sr));
                }
            }
            Check(stands == triggers.Length, string.Format("{0}: StandOn finds a floor at {1} of {2} trigger positions ({3} covered)", who, stands, triggers.Length, covered));
            WorldPoint start;
            double yaw, floorZ, ceilZ;
            int group, fg;
            string why;
            var sw = Stopwatch.StartNew();
            bool found = geo.StartPoint(2.0, out start, out yaw, out group, out why);
            double ms = sw.Elapsed.TotalMilliseconds;
            Check(found, who + ": a start point inside the world model (" + why + ")");
            if (found)
            {
                bool onFloor = geo.FloorBelow(start.X, start.Y, start.Z, out floorZ, out fg) && start.Z - floorZ > 0.4 && start.Z - floorZ <= 2.01;
                bool under = geo.CeilingAbove(start.X, start.Y, start.Z, out ceilZ) && ceilZ > start.Z;
                Check(onFloor && under && InsideAny(boxes, start.X, start.Y, start.Z, 0.0),
                      string.Format(CultureInfo.InvariantCulture, "{0}: start ({1:0.##}, {2:0.##}, {3:0.##}) yaw {4:0}: on a floor ({5:0.##}), under a " +
                                    "ceiling ({6:0.##}), inside group {7}'s box; found in {8:0.#} ms", who, start.X, start.Y, start.Z, yaw, floorZ, ceilZ, group, ms));
            }
        }

        static void TileTests(ClientFiles cf)
        {
            const uint obj0Id = 777623;
            string who = cf.Name + " " + cf.Build + " 30_47";
            if (!cf.Has(obj0Id))
            {
                Info(who + ": obj0 not in this client: skipped");
                return;
            }
            AdtObj0 obj0 = AdtParser.ParseObj0(cf.Read(obj0Id), 30, 47, "obj0 30_47");
            var notes = new List<string>();
            List<WorldModelPlacement> plan = WorldModelPlan.Build(obj0.Wmos, PlacementFrame.Tile, notes);
            int fitRoot = 0, fitGroups = 0, tilted = 0, unsupported = 0, n = 0;
            var misfits = new List<string>();
            foreach (WorldModelPlacement p in plan)
            {
                if (p.Unsupported != null) { unsupported++; continue; }
                n++;
                if (Math.Abs(p.Record.Rotation.A) > 1e-3 || Math.Abs(p.Record.Rotation.C) > 1e-3) tilted++;
                WmoRoot root = WmoParser.ParseRoot(cf.Read(p.FileDataID), "root " + p.FileDataID);
                WorldPoint rmin, rmax;
                WorldModelPlan.RootBox(root, p, out rmin, out rmax);
                double er = BoxError(rmin, rmax, p.ExtentsMin, p.ExtentsMax);
                // The group-box union (MOGI) placed: the audit's fallback for a root box that disagrees with its own groups.
                WorldModelGroupBox[] boxes = WorldModelPlan.GroupBoxes(root, p);
                var umin = new WorldPoint(double.MaxValue, double.MaxValue, double.MaxValue);
                var umax = new WorldPoint(double.MinValue, double.MinValue, double.MinValue);
                foreach (WorldModelGroupBox b in boxes)
                {
                    umin = new WorldPoint(Math.Min(umin.X, b.Min.X), Math.Min(umin.Y, b.Min.Y), Math.Min(umin.Z, b.Min.Z));
                    umax = new WorldPoint(Math.Max(umax.X, b.Max.X), Math.Max(umax.Y, b.Max.Y), Math.Max(umax.Z, b.Max.Z));
                }
                double eg = BoxError(umin, umax, p.ExtentsMin, p.ExtentsMax);
                // Every placed group box inside the extents (0.1 yd): the union may be smaller than the root box the record
                // stores, never larger.
                bool contained = true;
                foreach (WorldModelGroupBox b in boxes)
                    contained &= b.Min.X >= p.ExtentsMin.X - 0.1 && b.Min.Y >= p.ExtentsMin.Y - 0.1 && b.Min.Z >= p.ExtentsMin.Z - 0.1 &&
                                 b.Max.X <= p.ExtentsMax.X + 0.1 && b.Max.Y <= p.ExtentsMax.Y + 0.1 && b.Max.Z <= p.ExtentsMax.Z + 0.1;
                if (er <= 0.1) fitRoot++;
                if (contained) fitGroups++;
                if (eg > 0.1)
                    Info(string.Format(CultureInfo.InvariantCulture, "{0}: uniqueId {1} WMO {2}: its group boxes' union is {3:0.###} yd inside the extents " +
                                       "(the root box fits: {4:0.###} yd)", who, p.UniqueId, p.FileDataID, eg, er));
                if (er > 0.1)
                    misfits.Add(string.Format(CultureInfo.InvariantCulture, "uniqueId {0} WMO {1}: root box {2:0.###} yd off, group union {3:0.###}",
                                              p.UniqueId, p.FileDataID, er, eg));
            }
            Info(string.Format("{0}: {1} MODF record(s), {2} placement(s) ({3} unsupported, {4} tilted); {5} note(s)", who, obj0.Wmos.Length, plan.Count,
                               unsupported, tilted, notes.Count));
            foreach (string m in misfits)
                Info(who + ": misfit " + m);
            Check(n > 0 && fitRoot + misfits.Count == n, string.Format("{0}: {1} of {2} placed root boxes within 0.1 yd of the MODF extents", who, fitRoot, n));
            Check(n > 0 && fitGroups == n, string.Format("{0}: {1} of {2} placements have every placed group box inside the MODF extents", who, fitGroups, n));
        }

        public static int Run(string corpus, Action<string> outputTo)
        {
            failures = 0;
            output = outputTo ?? (s => { });
            string folder = Path.Combine(corpus, "world06");
            if (!File.Exists(Path.Combine(folder, "manifest.json")))
            {
                output("SKIPPED: " + folder + "/manifest.json is missing (python -I Tools/world-audit/world06_corpus.py <WoW install> <audit data>)");
                return 0;
            }
            Dictionary<string, ClientFiles> clients = LoadManifest(folder);
            foreach (string c in Clients)
            {
                ClientFiles cf;
                if (!clients.TryGetValue(c, out cf))
                {
                    output("  SKIP  " + c + ": not in the corpus manifest");
                    continue;
                }
                output("Client " + c + " " + cf.Build);
                try
                {
                    GlobalWmoTests(cf, 791060, 34, Map34Triggers[c], 0f);
                    GlobalWmoTests(cf, 780814, 429, c == "wow_classic_beta" ? Map429TriggersBeta : Map429TriggersAll, c == "wow_classic_era" ? 180f : 0f);
                    TileTests(cf);
                }
                catch (Exception e)
                {
                    Check(false, c + ": " + e.GetType().Name + ": " + e.Message);
                }
            }
            output(string.Format("WorldModelCorpusTests: {0} failure(s)", failures));
            return failures;
        }
    }
}
