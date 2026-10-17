// DoodadCorpusTests.cs
//
// Stage 07 on REAL client bytes (docs/world-viewer/07-doodads.md section 3): the corpus Tools/world-audit/world07_corpus.py
// extracts into <WMV_WORLD_AUDIT_CORPUS>/world07 -- per client the WDTs of maps 0, 34 and 429, tiles 30_47 and 31_48 (root,
// obj0, obj1), every world model they place with its LOD0 groups, every M2 their MDDF and MODI name, each M2's first skin
// and textures -- with a manifest of SHA-256s and the census (census.json) the budgets were chosen from. Every file is
// checked against the manifest before it is used (WorldModelCorpusTests.ClientFiles).
//
//   1. MDDF: DoodadPlan.FromMddf on each tile's obj0, every doodad's M2 header box (MD20 + 0xA0) placed by the plan and
//      compared with the client's own world box for it (obj1 MLDX, matched through MLDD by uniqueId): the audit's proof
//      (00-audit 6.4, Retail only) replayed through the player's code on all four clients, beside the same boxes placed
//      without the rule's half turn.
//   2. The plan's counts against the census: unique MDDF doodads, active WMO doodads per placement and per load (tiles and
//      the two WMO-only maps).
//   3. Doodad sets: no record of the global set 0 stands where a record of another set stands (set 0 adds, it never
//      replaces); the other sets overlap one another (alternatives); the selections on tile 30_47.
//   4. MODR: present exactly on groups with flag 0x800, every index inside MODD.
//   5. The MODD rotation convention: each doodad's M2 box placed in its world model by R(q) and by its transpose (and by
//      R(q) after a quarter or half turn); horizontal segments from the box's centre to its corners at 40 % and 75 % of its
//      height crossing a wall of the world model by more than a quarter of the box's half-size count as "in a wall". R(q)
//      puts fewer boxes in walls on every client -- the same counts as the independent probe the convention was chosen by.
//   6. Reference cases: pinned world positions of real doodads (an MDDF doodad, a doodad of world model 892927 in both of
//      its placements), computed independently.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text.Json;
using Wmv.Wow;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class DoodadCorpusTests
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

        // Tile -> (obj0, obj1) FileDataIDs, the same in every client (map 0's MAID).
        static readonly int[][] Tiles = { new[] { 30, 47, 777623, 777624 }, new[] { 31, 48, 777828, 777829 } };

        /// <summary>The M2's header box (MD20 + 0xA0, model space), or null.</summary>
        static double[] M2Box(byte[] b)
        {
            int bs = b.Length >= 16 && b[0] == 'M' && b[1] == 'D' && b[2] == '2' && b[3] == '1' ? 8 : 0;
            if (b.Length < bs + 0xA0 + 24)
                return null;
            var r = new double[6];
            for (int i = 0; i < 6; i++)
                r[i] = BitConverter.ToSingle(b, bs + 0xA0 + 4 * i);
            return r;
        }

        static Dictionary<uint, double[]> boxCache;

        static double[] BoxOf(WorldModelCorpusTests.ClientFiles cf, uint fid)
        {
            double[] box;
            if (boxCache.TryGetValue(fid, out box))
                return box;
            box = cf.Has(fid) ? M2Box(cf.Read(fid)) : null;
            boxCache[fid] = box;
            return box;
        }

        // ------------------------------------------------------------------------------------------------ 1. MDDF

        static void MddfTests(WorldModelCorpusTests.ClientFiles cf, JsonElement census)
        {
            foreach (int[] t in Tiles)
            {
                string who = string.Format("{0} {1} {2}_{3}", cf.Name, cf.Build, t[0], t[1]);
                AdtObj0 obj0 = AdtParser.ParseObj0(cf.Read((uint)t[2]), t[0], t[1], "obj0");
                AdtObj1 obj1 = AdtParser.ParseObj1(cf.Read((uint)t[3]), t[0], t[1], "obj1");
                var notes = new List<string>();
                var sw = Stopwatch.StartNew();
                List<WorldDoodad> plan = DoodadPlan.FromMddf(obj0.Doodads, PlacementFrame.Tile, notes);
                double ms = sw.Elapsed.TotalMilliseconds;
                JsonElement tile = census.GetProperty("tiles").GetProperty(t[0] + "_" + t[1]);
                int unsupported = 0;
                foreach (WorldDoodad d in plan) if (d.Unsupported != null) unsupported++;
                Check(plan.Count == tile.GetProperty("mddfUnique").GetInt32() && unsupported == 0 && notes.Count == 0,
                      string.Format("{0}: {1} MDDF record(s) -> {2} doodad(s) by uniqueId, {3} unsupported, {4} note(s) (census: {5} unique); planned in {6:0.##} ms",
                                    who, obj0.Doodads.Length, plan.Count, unsupported, notes.Count, tile.GetProperty("mddfUnique").GetInt32(), ms));
                if (plan.Count == 0)
                {
                    Info(who + ": no outdoor doodads on this tile (the census agrees): the MLDX replay has nothing to place");
                    continue;
                }
                var byId = new Dictionary<uint, WorldDoodad>();
                foreach (WorldDoodad d in plan) byId[d.UniqueId] = d;
                Check(obj1.DoodadBoxes != null && obj1.DoodadBoxes.Length == obj1.Doodads.Length, who + ": obj1 has one MLDX box per MLDD record");
                if (obj1.DoodadBoxes == null)
                    continue;
                int items = 0, within = 0, tilted = 0, tiltedWithin = 0, moved = 0, movedRule = 0, movedHalf = 0, noBox = 0, notInPlan = 0;
                double worst = 0;
                for (int i = 0; i < obj1.Doodads.Length; i++)
                {
                    WorldDoodad d;
                    if (!byId.TryGetValue(obj1.Doodads[i].UniqueId, out d)) { notInPlan++; continue; }
                    double[] bx = BoxOf(cf, d.FileDataID);
                    if (bx == null) { noBox++; continue; }
                    DoodadWorldBox want = obj1.DoodadBoxes[i];
                    WorldPoint mn, mx;
                    d.PlaceBox(new WorldPoint(bx[0], bx[1], bx[2]), new WorldPoint(bx[3], bx[4], bx[5]), out mn, out mx);
                    double e = Err(mn, mx, want);
                    items++;
                    bool tilt = false;
                    foreach (DoodadPlacement r in obj0.Doodads)
                        if (r.UniqueId == d.UniqueId) { tilt = Math.Abs(r.Rotation.A) > 0.01 || Math.Abs(r.Rotation.C) > 0.01; break; }
                    if (e <= 0.5) { within++; if (tilt) tiltedWithin++; }
                    else worst = Math.Max(worst, e);
                    if (tilt) tilted++;
                    // The same box without the rule's half turn (R = Rz(a1) Ry(a0) Rx(a2)). A box centred on its model's origin
                    // lands the same either way; only the boxes the half turn moves by more than a yard can tell.
                    var alt = new WorldDoodad { Position = d.Position, Scale = d.Scale, Rotation = PlacementTransform.Mul(PlacementTransform.Rot(2, 180.0), d.Rotation) };
                    WorldPoint amn, amx;
                    alt.PlaceBox(new WorldPoint(bx[0], bx[1], bx[2]), new WorldPoint(bx[3], bx[4], bx[5]), out amn, out amx);
                    if (Err(amn, amx, mn, mx) > 1.0)
                    {
                        moved++;
                        if (e <= 0.5) movedRule++;
                        if (Err(amn, amx, want) <= 0.5) movedHalf++;
                    }
                }
                Info(string.Format(CultureInfo.InvariantCulture, "{0}: MLDD {1} record(s): {2} compared, {3} not in the plan, {4} without an M2 box; worst misfit {5:0.###} yd",
                                   who, obj1.Doodads.Length, items, notInPlan, noBox, worst));
                Check(items > 0 && within >= items * 0.99 && tiltedWithin >= tilted * 0.99,
                      string.Format("{0}: {1} of {2} placed M2 boxes within 0.5 yd of the client's MLDX box ({3} of {4} tilted) -- Tile frame, Rz(a1+180) Ry(a0) Rx(a2), u16/1024",
                                    who, within, items, tiltedWithin, tilted));
                Check(moved > 0 && movedRule == moved && movedHalf == 0,
                      string.Format("{0}: of the {1} boxes the half turn moves by more than a yard, {2} fit with the rule's +180 and {3} without it",
                                    who, moved, movedRule, movedHalf));
            }
        }

        static double Err(WorldPoint mn, WorldPoint mx, WorldPoint bmn, WorldPoint bmx)
        {
            double e = 0;
            e = Math.Max(e, Math.Abs(mn.X - bmn.X)); e = Math.Max(e, Math.Abs(mn.Y - bmn.Y)); e = Math.Max(e, Math.Abs(mn.Z - bmn.Z));
            e = Math.Max(e, Math.Abs(mx.X - bmx.X)); e = Math.Max(e, Math.Abs(mx.Y - bmx.Y)); e = Math.Max(e, Math.Abs(mx.Z - bmx.Z));
            return e;
        }

        static double Err(WorldPoint mn, WorldPoint mx, DoodadWorldBox want)
        {
            double e = 0;
            e = Math.Max(e, Math.Abs(mn.X - want.Min.A)); e = Math.Max(e, Math.Abs(mn.Y - want.Min.B)); e = Math.Max(e, Math.Abs(mn.Z - want.Min.C));
            e = Math.Max(e, Math.Abs(mx.X - want.Max.A)); e = Math.Max(e, Math.Abs(mx.Y - want.Max.B)); e = Math.Max(e, Math.Abs(mx.Z - want.Max.C));
            return e;
        }

        // ------------------------------------------------------------------------------------------------ 2. counts

        static int WmoDoodads(WorldModelCorpusTests.ClientFiles cf, List<WorldModelPlacement> placements, Dictionary<uint, int> perPlacement,
                              List<WmoDoodadSelection> selections, out int unsupported)
        {
            int total = 0;
            unsupported = 0;
            for (int i = 0; i < placements.Count; i++)
            {
                WorldModelPlacement p = placements[i];
                if (p.Unsupported != null || !cf.Has(p.FileDataID))
                    continue;
                WmoRoot root = WmoParser.ParseRoot(cf.Read(p.FileDataID), "root " + p.FileDataID);
                WmoDoodadSelection sel = DoodadPlan.SelectSets(root, p.UniqueId, p.FileDataID, p.Record.Word58);
                List<WorldDoodad> list = DoodadPlan.FromWmo(p, i, root, sel);
                if (selections != null) selections.Add(sel);
                if (list.Count != sel.Records)
                    Check(false, string.Format("{0}: WMO {1} uniqueId {2}: {3} doodads planned, the selection counts {4}", cf.Name, p.FileDataID, p.UniqueId, list.Count, sel.Records));
                foreach (WorldDoodad d in list) if (d.Unsupported != null) unsupported++;
                if (perPlacement != null) perPlacement[p.UniqueId] = list.Count;
                total += list.Count;
            }
            return total;
        }

        static void CountTests(WorldModelCorpusTests.ClientFiles cf, JsonElement census)
        {
            foreach (int[] t in Tiles)
            {
                string who = string.Format("{0} {1} {2}_{3}", cf.Name, cf.Build, t[0], t[1]);
                AdtObj0 obj0 = AdtParser.ParseObj0(cf.Read((uint)t[2]), t[0], t[1], "obj0");
                List<WorldModelPlacement> placements = WorldModelPlan.Build(obj0.Wmos, PlacementFrame.Tile, null);
                var per = new Dictionary<uint, int>();
                var sels = new List<WmoDoodadSelection>();
                int unsupported;
                var sw = Stopwatch.StartNew();
                int wmo = WmoDoodads(cf, placements, per, sels, out unsupported);
                double ms = sw.Elapsed.TotalMilliseconds;
                JsonElement tile = census.GetProperty("tiles").GetProperty(t[0] + "_" + t[1]);
                int mismatched = 0, censusPlacements = 0;
                foreach (JsonElement cp in tile.GetProperty("wmoPlacements").EnumerateArray())
                {
                    censusPlacements++;
                    int n;
                    if (!per.TryGetValue(cp.GetProperty("uniqueId").GetUInt32(), out n) || n != cp.GetProperty("doodadsActive").GetInt32())
                        mismatched++;
                }
                int mddf = DoodadPlan.FromMddf(obj0.Doodads, PlacementFrame.Tile, null).Count;
                Check(mismatched == 0 && censusPlacements == per.Count && wmo == tile.GetProperty("wmoDoodadInstancesActive").GetInt32() &&
                      mddf + wmo == tile.GetProperty("instancesTotal").GetInt32(),
                      string.Format(CultureInfo.InvariantCulture, "{0}: {1} world model placement(s) -> {2} active WMO doodad(s) ({3} unsupported), {4} MDDF, {5} in all " +
                                    "(census: {6} + {7} = {8}; {9} placement(s) differ); planned in {10:0.##} ms",
                                    who, per.Count, wmo, unsupported, mddf, mddf + wmo, tile.GetProperty("mddfUnique").GetInt32(),
                                    tile.GetProperty("wmoDoodadInstancesActive").GetInt32(), tile.GetProperty("instancesTotal").GetInt32(), mismatched, ms));
                int reasons = 0;
                foreach (WmoDoodadSelection s in sels)
                    if (s.Reason != null && s.SetCount > 0) { reasons++; Info(who + ": WMO " + s.WmoFileDataID + " uniqueId " + s.UniqueId + ": " + s.Reason); }
                if (t[0] == 30)
                    foreach (WmoDoodadSelection s in sels)
                        if (s.Active.Count > 1 || s.Requested != 0)
                            Info(string.Format("{0}: WMO {1} uniqueId {2}: placement set {3} of {4} -> sets [{5}] ({6}), {7} record(s)", who, s.WmoFileDataID, s.UniqueId,
                                               s.Requested, s.SetCount, string.Join(", ", s.Active), string.Join(", ", s.Names), s.Records));
                Check(reasons == 0, string.Format("{0}: every placement names a set its world model has ({1} remark(s))", who, reasons));
            }
            foreach (uint wdtId in new uint[] { 791060, 780814 })
            {
                string who = string.Format("{0} {1} WDT {2}", cf.Name, cf.Build, wdtId);
                WdtFile wdt = WdtParser.Parse(cf.Read(wdtId), "wdt " + wdtId);
                List<WorldModelPlacement> placements = WorldModelPlan.Build(wdt.GlobalWmos, PlacementFrame.Global, null);
                int unsupported;
                int wmo = WmoDoodads(cf, placements, null, null, out unsupported);
                JsonElement map = census.GetProperty("wmoOnlyMaps").GetProperty(wdtId.ToString(CultureInfo.InvariantCulture));
                Check(wmo == map.GetProperty("instancesActive").GetInt32(), string.Format("{0}: the global world model's active sets hold {1} doodad(s) ({2} unsupported; census {3})",
                                                                                          who, wmo, unsupported, map.GetProperty("instancesActive").GetInt32()));
            }
        }

        // ------------------------------------------------------------------------------------------------ 3. sets, 4. MODR

        static List<uint> Roots(WorldModelCorpusTests.ClientFiles cf)
        {
            var roots = new List<uint>();
            foreach (KeyValuePair<uint, string> kv in cf.Role)
                if (kv.Value == "wmo root")
                    roots.Add(kv.Key);
            roots.Sort();
            return roots;
        }

        static bool SameSpot(WmoRoot root, WmoDoodadDef a, WmoDoodadDef b)
        {
            uint ma = a.NameIndex < root.DoodadFileDataIDs.Length ? root.DoodadFileDataIDs[a.NameIndex] : 0;
            uint mb = b.NameIndex < root.DoodadFileDataIDs.Length ? root.DoodadFileDataIDs[b.NameIndex] : 0;
            return ma == mb && Math.Abs(a.Position.X - b.Position.X) < 0.01 && Math.Abs(a.Position.Y - b.Position.Y) < 0.01 &&
                   Math.Abs(a.Position.Z - b.Position.Z) < 0.01;
        }

        static void SetAndModrTests(WorldModelCorpusTests.ClientFiles cf)
        {
            string who = cf.Name + " " + cf.Build;
            int roots = 0, multiSet = 0, globalHits = 0, altRecords = 0, altHits = 0;
            int groups = 0, withModr = 0, flagMismatch = 0, badIndex = 0, shared = 0, unlisted = 0, records = 0, spanning = 0;
            foreach (uint id in Roots(cf))
            {
                WmoRoot root = WmoParser.ParseRoot(cf.Read(id), "root " + id);
                roots++;
                WmoDoodadSet[] sets = root.DoodadSets;
                WmoDoodadDef[] defs = root.DoodadDefs;
                if (sets.Length > 1)
                {
                    multiSet++;
                    long g0 = sets[0].StartIndex, g1 = Math.Min(g0 + sets[0].Count, defs.Length);
                    for (int s = 1; s < sets.Length; s++)
                    {
                        long a0 = sets[s].StartIndex, a1 = Math.Min(a0 + sets[s].Count, defs.Length);
                        for (long i = a0; i < a1; i++)
                        {
                            for (long g = g0; g < g1; g++)
                                if (SameSpot(root, defs[i], defs[g])) { globalHits++; break; }
                            // Does another non-global set hold the same model on the same spot?
                            altRecords++;
                            bool hit = false;
                            for (int o = 1; o < sets.Length && !hit; o++)
                            {
                                if (o == s) continue;
                                long b0 = sets[o].StartIndex, b1 = Math.Min(b0 + sets[o].Count, defs.Length);
                                for (long j = b0; j < b1 && !hit; j++)
                                    hit = SameSpot(root, defs[i], defs[j]);
                            }
                            if (hit) altHits++;
                        }
                    }
                }
                // MODR on the LOD0 groups in the corpus.
                uint[] gids = root.Lod0GroupFileDataIDs;
                var parsed = new WmoGroup[gids.Length];
                for (int g = 0; g < gids.Length; g++)
                {
                    if (gids[g] == 0 || !cf.Has(gids[g])) continue;
                    WmoGroup grp = WmoParser.ParseGroup(cf.Read(gids[g]), "group " + gids[g], g);
                    parsed[g] = grp;
                    groups++;
                    WmoChunkInfo c;
                    bool has = grp.TryGetChunk("MODR", out c);
                    if (has) withModr++;
                    if (has != ((grp.Header.Flags & 0x800) != 0)) flagMismatch++;
                    if (has)
                    {
                        byte[] b = grp.CopyChunkBytes(c);
                        var setsSeen = new HashSet<int>();
                        for (int i = 0; i + 1 < b.Length; i += 2)
                        {
                            int k = b[i] | (b[i + 1] << 8);
                            if (k >= defs.Length) { badIndex++; continue; }
                            for (int s = 0; s < sets.Length; s++)
                                if (k >= sets[s].StartIndex && k < sets[s].StartIndex + sets[s].Count) { setsSeen.Add(s); break; }
                        }
                        if (setsSeen.Count > 1) spanning++;
                    }
                }
                List<int>[] owners = DoodadPlan.GroupMembership(root, parsed);
                for (int k = 0; k < owners.Length; k++)
                {
                    records++;
                    if (owners[k] == null) unlisted++;
                    else if (owners[k].Count > 1) shared++;
                }
            }
            Check(globalHits == 0, string.Format("{0}: {1} root(s), {2} with several sets: no record of another set stands where a set-0 record stands " +
                                                 "({3} do) -- the global set adds to the chosen set, it is not one of the alternatives", who, roots, multiSet, globalHits));
            Info(string.Format("{0}: {1} of {2} records of the non-global sets stand where another non-global set has the same model (alternatives)",
                               who, altHits, altRecords));
            Check(groups > 0 && flagMismatch == 0 && badIndex == 0,
                  string.Format("{0}: {1} LOD0 group(s), {2} with MODR: MODR present exactly when flag 0x800 is set ({3} differ), every index inside MODD ({4} outside)",
                                who, groups, withModr, flagMismatch, badIndex));
            Info(string.Format("{0}: of {1} MODD record(s) {2} are listed by several groups, {3} by none; {4} group(s) list records of several sets",
                               who, records, shared, unlisted, spanning));
        }

        // ------------------------------------------------------------------------------------------------ 5. walls

        /// <summary>The world model's wall triangles (|n.z| below half |n|), with a 2D grid over their boxes.</summary>
        sealed class Walls
        {
            const double Cell = 8.0;
            public readonly List<double> V = new List<double>();
            readonly List<double> lo = new List<double>(), hi = new List<double>();
            readonly Dictionary<long, List<int>> cells = new Dictionary<long, List<int>>();
            int[] stamp;
            int tick;
            public int Count { get { return lo.Count / 3; } }

            public void Add(WmoGroup g)
            {
                WowVec3[] p = g.Positions;
                ushort[] ix = g.Indices;
                for (int t = 0; t + 2 < ix.Length; t += 3)
                {
                    if (ix[t] >= p.Length || ix[t + 1] >= p.Length || ix[t + 2] >= p.Length) continue;
                    WowVec3 a = p[ix[t]], b = p[ix[t + 1]], c = p[ix[t + 2]];
                    double e1x = (double)b.X - a.X, e1y = (double)b.Y - a.Y, e1z = (double)b.Z - a.Z;
                    double e2x = (double)c.X - a.X, e2y = (double)c.Y - a.Y, e2z = (double)c.Z - a.Z;
                    double nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
                    double nl = Math.Sqrt(nx * nx + ny * ny + nz * nz);
                    if (!(Math.Abs(nz) < 0.5 * Math.Max(nl, 1e-12))) continue;
                    int k = Count;
                    V.Add(a.X); V.Add(a.Y); V.Add(a.Z); V.Add(b.X); V.Add(b.Y); V.Add(b.Z); V.Add(c.X); V.Add(c.Y); V.Add(c.Z);
                    double x0 = Math.Min(a.X, Math.Min(b.X, c.X)), y0 = Math.Min(a.Y, Math.Min(b.Y, c.Y)), z0 = Math.Min(a.Z, Math.Min(b.Z, c.Z));
                    double x1 = Math.Max(a.X, Math.Max(b.X, c.X)), y1 = Math.Max(a.Y, Math.Max(b.Y, c.Y)), z1 = Math.Max(a.Z, Math.Max(b.Z, c.Z));
                    lo.Add(x0); lo.Add(y0); lo.Add(z0); hi.Add(x1); hi.Add(y1); hi.Add(z1);
                    for (long cx = (long)Math.Floor(x0 / Cell); cx <= (long)Math.Floor(x1 / Cell); cx++)
                        for (long cy = (long)Math.Floor(y0 / Cell); cy <= (long)Math.Floor(y1 / Cell); cy++)
                        {
                            long key = (cx << 32) ^ (cy & 0xFFFFFFFF);
                            List<int> l;
                            if (!cells.TryGetValue(key, out l)) cells[key] = l = new List<int>();
                            l.Add(k);
                        }
                }
            }

            /// <summary>The wall triangles whose box meets [a, b].</summary>
            public void Near(double[] a, double[] b, List<int> into)
            {
                into.Clear();
                if (stamp == null || stamp.Length < Count) stamp = new int[Count];
                tick++;
                for (long cx = (long)Math.Floor(a[0] / Cell); cx <= (long)Math.Floor(b[0] / Cell); cx++)
                    for (long cy = (long)Math.Floor(a[1] / Cell); cy <= (long)Math.Floor(b[1] / Cell); cy++)
                    {
                        List<int> l;
                        if (!cells.TryGetValue((cx << 32) ^ (cy & 0xFFFFFFFF), out l)) continue;
                        foreach (int k in l)
                        {
                            if (stamp[k] == tick) continue;
                            stamp[k] = tick;
                            if (hi[3 * k] >= a[0] && lo[3 * k] <= b[0] && hi[3 * k + 1] >= a[1] && lo[3 * k + 1] <= b[1] && hi[3 * k + 2] >= a[2] && lo[3 * k + 2] <= b[2])
                                into.Add(k);
                        }
                    }
            }

            /// <summary>How far past the first wall it crosses the segment p0 -> p1 ends (yards); 0 when it crosses none.</summary>
            public double Crosses(double[] p0, double[] p1, List<int> near)
            {
                double dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
                double best = double.MaxValue;
                foreach (int k in near)
                {
                    int o = 9 * k;
                    double tx = V[o], ty = V[o + 1], tz = V[o + 2];
                    double e1x = V[o + 3] - tx, e1y = V[o + 4] - ty, e1z = V[o + 5] - tz;
                    double e2x = V[o + 6] - tx, e2y = V[o + 7] - ty, e2z = V[o + 8] - tz;
                    double hx = dy * e2z - dz * e2y, hy = dz * e2x - dx * e2z, hz = dx * e2y - dy * e2x;
                    double a = e1x * hx + e1y * hy + e1z * hz;
                    if (!(Math.Abs(a) > 1e-12)) continue;
                    double f = 1.0 / a;
                    double sx = p0[0] - tx, sy = p0[1] - ty, sz = p0[2] - tz;
                    double u = f * (sx * hx + sy * hy + sz * hz);
                    double qx = sy * e1z - sz * e1y, qy = sz * e1x - sx * e1z, qz = sx * e1y - sy * e1x;
                    double v = f * (qx * dx + qy * dy + qz * dz);
                    double t = f * (e2x * qx + e2y * qy + e2z * qz);
                    if (u >= 0 && u <= 1 && v >= 0 && u + v <= 1 && t > 1e-6 && t < 1 - 1e-6 && t < best)
                        best = t;
                }
                return best == double.MaxValue ? 0.0 : (1.0 - best) * Math.Sqrt(dx * dx + dy * dy + dz * dz);
            }
        }

        static double[] Rz(double degrees)
        {
            double a = degrees * Math.PI / 180.0, c = Math.Cos(a), s = Math.Sin(a);
            return new[] { c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0 };
        }

        static double[] Transpose(double[] m) { return new[] { m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8] }; }

        // Conventions: R(q), R(q)^T, R(q) Rz(90), R(q) Rz(180), R(q) Rz(270).
        static readonly string[] Conventions = { "R(q)", "R(q)^T", "R(q)Rz90", "R(q)Rz180", "R(q)Rz270" };

        static bool InWall(Walls w, double[] m, double[] pos, double sc, double[] bx, List<int> near)
        {
            double sx = bx[3] - bx[0], sy = bx[4] - bx[1], sz = bx[5] - bx[2];
            double limit = Math.Max(0.25, 0.25 * 0.5 * sc * Math.Max(sx, sy));
            foreach (double frac in new[] { 0.4, 0.75 })
            {
                double zc = bx[2] + frac * sz;
                double[] centre = Place(m, pos, sc, (bx[0] + bx[3]) / 2, (bx[1] + bx[4]) / 2, zc);
                var pts = new double[4][];
                int n = 0;
                foreach (double cx in new[] { bx[0], bx[3] })
                    foreach (double cy in new[] { bx[1], bx[4] })
                        pts[n++] = Place(m, pos, sc, cx, cy, zc);
                double[] lo = (double[])centre.Clone(), hi = (double[])centre.Clone();
                foreach (double[] p in pts)
                    for (int i = 0; i < 3; i++) { lo[i] = Math.Min(lo[i], p[i]); hi[i] = Math.Max(hi[i], p[i]); }
                for (int i = 0; i < 3; i++) { lo[i] -= 0.05; hi[i] += 0.05; }
                w.Near(lo, hi, near);
                foreach (double[] p in pts)
                    if (w.Crosses(centre, p, near) > limit)
                        return true;
            }
            return false;
        }

        static double[] Place(double[] m, double[] pos, double sc, double x, double y, double z)
        {
            return new[] { pos[0] + sc * (m[0] * x + m[1] * y + m[2] * z), pos[1] + sc * (m[3] * x + m[4] * y + m[5] * z), pos[2] + sc * (m[6] * x + m[7] * y + m[8] * z) };
        }

        // The independent probe's counts (07-doodads section 3.3), per box family: boxes tested, then in a wall under R(q), R(q)^T,
        // R(q)Rz90, R(q)Rz180, R(q)Rz270; for the elongated boxes also in a wall under only R(q), only R(q)^T.
        static readonly Dictionary<string, int[]> ProbeElongated = new Dictionary<string, int[]>
        {
            { "wow", new[] { 1735, 310, 493, 793, 371, 753, 33, 216 } }, { "wow_classic_era", new[] { 2023, 215, 493, 850, 276, 792, 43, 321 } },
            { "wow_classic", new[] { 1595, 250, 415, 682, 301, 654, 31, 196 } }, { "wow_classic_beta", new[] { 2633, 354, 660, 1225, 349, 1168, 82, 388 } },
        };
        static readonly Dictionary<string, int[]> ProbeOffCentre = new Dictionary<string, int[]>
        {
            { "wow", new[] { 462, 65, 139, 211, 137, 191 } }, { "wow_classic_era", new[] { 772, 91, 178, 292, 161, 255 } },
            { "wow_classic", new[] { 440, 55, 127, 195, 127, 179 } }, { "wow_classic_beta", new[] { 782, 93, 183, 293, 170, 262 } },
        };

        static void WallTests(WorldModelCorpusTests.ClientFiles cf)
        {
            string who = cf.Name + " " + cf.Build;
            var sw = Stopwatch.StartNew();
            int elongated = 0, offCentre = 0, onlyR = 0, onlyRT = 0;
            var badE = new int[Conventions.Length];
            var badO = new int[Conventions.Length];
            var near = new List<int>();
            foreach (uint id in Roots(cf))
            {
                WmoRoot root = WmoParser.ParseRoot(cf.Read(id), "root " + id);
                if (root.DoodadDefs.Length == 0 || root.DoodadFileDataIDs.Length == 0)
                    continue;
                var walls = new Walls();
                foreach (uint g in root.Lod0GroupFileDataIDs)
                    if (g != 0 && cf.Has(g))
                        walls.Add(WmoParser.ParseGroup(cf.Read(g), "group " + g));
                if (walls.Count == 0)
                    continue;
                foreach (WmoDoodadDef def in root.DoodadDefs)
                {
                    uint mf = def.NameIndex < root.DoodadFileDataIDs.Length ? root.DoodadFileDataIDs[def.NameIndex] : 0;
                    if (mf == 0 || !cf.Has(mf))
                        continue;
                    double[] bx = BoxOf(cf, mf);
                    if (bx == null)
                        continue;
                    double sx = bx[3] - bx[0], sy = bx[4] - bx[1], sz = bx[5] - bx[2], sc = def.Scale;
                    if (Math.Min(sx, sy) <= 0 || Math.Max(sx, sy) * sc < 0.6 || sz * sc < 0.4)
                        continue;
                    bool isElongated = Math.Max(sx, sy) >= 1.5 * Math.Min(sx, sy);
                    bool isOffCentre = Math.Abs((bx[0] + bx[3]) / 2) > 0.3 * sx || Math.Abs((bx[1] + bx[4]) / 2) > 0.3 * sy;
                    if (!isElongated && !isOffCentre)
                        continue;
                    double[] pos = { def.Position.X, def.Position.Y, def.Position.Z };
                    double[] r = DoodadPlan.QuaternionMatrix(def.Rotation);
                    double[][] ms = { r, Transpose(r), PlacementTransform.Mul(r, Rz(90)), PlacementTransform.Mul(r, Rz(180)), PlacementTransform.Mul(r, Rz(270)) };
                    var bad = new bool[ms.Length];
                    for (int c = 0; c < ms.Length; c++)
                        bad[c] = InWall(walls, ms[c], pos, sc, bx, near);
                    if (isElongated)
                    {
                        elongated++;
                        for (int c = 0; c < ms.Length; c++) if (bad[c]) badE[c]++;
                        if (bad[0] && !bad[1]) onlyR++;
                        if (bad[1] && !bad[0]) onlyRT++;
                    }
                    if (isOffCentre)
                    {
                        offCentre++;
                        for (int c = 0; c < ms.Length; c++) if (bad[c]) badO[c]++;
                    }
                }
            }
            double ms2 = sw.Elapsed.TotalMilliseconds;
            Info(string.Format(CultureInfo.InvariantCulture, "{0}: elongated boxes {1}: in a wall {2}; only R(q) {3}, only R(q)^T {4}", who, elongated,
                               Describe(badE), onlyR, onlyRT));
            Info(string.Format(CultureInfo.InvariantCulture, "{0}: off-centre boxes {1}: in a wall {2}; {3:0} ms", who, offCentre, Describe(badO), ms2));
            Check(badE[0] < badE[1] && onlyR * 3 < onlyRT,
                  string.Format("{0}: R(q) puts fewer elongated wall-side doodads in walls than its transpose ({1} vs {2}; only one of them: {3} vs {4})",
                                who, badE[0], badE[1], onlyR, onlyRT));
            Check(badO[0] < badO[1] && badO[0] < badO[2] && badO[0] < badO[3] && badO[0] < badO[4],
                  string.Format("{0}: on off-centre boxes R(q) beats its transpose and every quarter or half turn after it ({1})", who, Describe(badO)));
            int[] pe, po;
            if (ProbeElongated.TryGetValue(cf.Name, out pe) && ProbeOffCentre.TryGetValue(cf.Name, out po))
            {
                bool same = elongated == pe[0] && onlyR == pe[6] && onlyRT == pe[7] && offCentre == po[0];
                for (int c = 0; c < Conventions.Length; c++)
                    same &= badE[c] == pe[1 + c] && badO[c] == po[1 + c];
                Check(same, string.Format("{0}: the same counts as the independent probe (elongated {1}: {2}; off-centre {3}: {4})",
                                          who, pe[0], Slice(pe, 1, 7), po[0], Slice(po, 1, 5)));
            }
        }

        static string Slice(int[] v, int start, int count)
        {
            var parts = new string[count];
            for (int i = 0; i < count; i++) parts[i] = v[start + i].ToString(CultureInfo.InvariantCulture);
            return string.Join("/", parts);
        }

        static string Describe(int[] bad)
        {
            var parts = new List<string>();
            for (int c = 0; c < bad.Length; c++) parts.Add(Conventions[c] + " " + bad[c]);
            return string.Join(", ", parts);
        }

        // ------------------------------------------------------------------------------------------------ 6. reference cases

        static void ReferenceCases(WorldModelCorpusTests.ClientFiles cf)
        {
            // Retail tile 30_47: the first MDDF doodad, and MODD record 29 (first of Set_InteriorDoodads) of world model 892927
            // in each of its two placements. Positions computed independently from the raw bytes (07-doodads section 3.6).
            if (cf.Name != "wow")
                return;
            string who = cf.Name + " " + cf.Build + " 30_47";
            AdtObj0 obj0 = AdtParser.ParseObj0(cf.Read(777623), 30, 47, "obj0");
            List<WorldDoodad> mddf = DoodadPlan.FromMddf(obj0.Doodads, PlacementFrame.Tile, null);
            WorldDoodad d0 = mddf[0];
            WorldPoint o0 = d0.ToWorld(1, 0, 0);
            Check(d0.UniqueId == 5496790 && d0.FileDataID == 189929 && Near(d0.Position, -8028.079427083332, 608.2779947916679, 85.13360595703125) &&
                  Math.Abs(d0.Scale - 1.12890625) < 1e-12 && Near(o0, -8027.584547155902, 607.2633405753379, 85.13360595703125),
                  string.Format(CultureInfo.InvariantCulture, "{0}: MDDF[0] uniqueId {1} M2 {2}: W ({3:0.####}, {4:0.####}, {5:0.####}) scale {6}; model (1, 0, 0) at " +
                                "({7:0.####}, {8:0.####}, {9:0.####}) -- the independent values", who, d0.UniqueId, d0.FileDataID, d0.Position.X, d0.Position.Y,
                                d0.Position.Z, d0.Scale, o0.X, o0.Y, o0.Z));
            // uniqueId -> W, then model (1, 0, 0); both MODD 29 of set 2 (Set_InteriorDoodads), M2 307027, scale 0.34632396697998047.
            var expect = new Dictionary<uint, double[]>
            {
                { 19042379, new[] { -8047.2322479852255, 824.5370679936084, 68.56230211257935, -8047.066726336916, 824.5086106403512, 68.86517676171863 } },
                { 19042630, new[] { -8254.359731223787, 766.3078164626704, 79.90451097488403, -8254.22089667502, 766.4023268064552, 80.20738562402332 } },
            };
            int seen = 0;
            List<WorldModelPlacement> placements = WorldModelPlan.Build(obj0.Wmos, PlacementFrame.Tile, null);
            for (int i = 0; i < placements.Count; i++)
            {
                WorldModelPlacement p = placements[i];
                if (p.FileDataID != 892927) continue;
                WmoRoot root = WmoParser.ParseRoot(cf.Read(p.FileDataID), "root");
                WmoDoodadSelection sel = DoodadPlan.SelectSets(root, p.UniqueId, p.FileDataID, p.Record.Word58);
                foreach (WorldDoodad d in DoodadPlan.FromWmo(p, i, root, sel))
                {
                    if (d.ModdIndex != 29) continue;
                    WorldPoint o = d.ToWorld(1, 0, 0);
                    double[] x;
                    seen++;
                    Check(expect.TryGetValue(p.UniqueId, out x) && d.Set == 2 && d.FileDataID == 307027 && Math.Abs(d.Scale - 0.34632396697998047) < 1e-12 &&
                          Near(d.Position, x[0], x[1], x[2]) && Near(o, x[3], x[4], x[5]),
                          string.Format(CultureInfo.InvariantCulture, "{0}: WMO 892927 uniqueId {1} MODD 29 (set {2}) M2 {3}: W ({4:0.####}, {5:0.####}, {6:0.####}) " +
                                        "scale {7:0.######}; model (1, 0, 0) at ({8:0.####}, {9:0.####}, {10:0.####}) -- the independent values", who, p.UniqueId,
                                        d.Set, d.FileDataID, d.Position.X, d.Position.Y, d.Position.Z, d.Scale, o.X, o.Y, o.Z));
                }
            }
            Check(seen == 2, who + ": world model 892927 is placed twice, each with its own MODD 29 (" + seen + " found)");
        }

        static bool Near(WorldPoint p, double x, double y, double z)
        {
            return Math.Abs(p.X - x) < 1e-6 && Math.Abs(p.Y - y) < 1e-6 && Math.Abs(p.Z - z) < 1e-6;
        }

        public static int Run(string corpus, Action<string> outputTo)
        {
            failures = 0;
            output = outputTo ?? (s => { });
            string folder = Path.Combine(corpus, "world07");
            if (!File.Exists(Path.Combine(folder, "manifest.json")) || !File.Exists(Path.Combine(folder, "census.json")))
            {
                output("SKIPPED: " + folder + "/manifest.json or census.json is missing (python -I Tools/world-audit/world07_corpus.py <WoW install> <audit data>)");
                return 0;
            }
            Dictionary<string, WorldModelCorpusTests.ClientFiles> clients = WorldModelCorpusTests.LoadManifest(folder);
            using (JsonDocument census = JsonDocument.Parse(File.ReadAllText(Path.Combine(folder, "census.json"))))
                foreach (string c in Clients)
                {
                    WorldModelCorpusTests.ClientFiles cf;
                    if (!clients.TryGetValue(c, out cf))
                    {
                        output("  SKIP  " + c + ": not in the corpus manifest");
                        continue;
                    }
                    output("Client " + c + " " + cf.Build);
                    boxCache = new Dictionary<uint, double[]>();
                    JsonElement cc = census.RootElement.GetProperty("clients").GetProperty(c);
                    Action[] steps = { () => MddfTests(cf, cc), () => CountTests(cf, cc), () => SetAndModrTests(cf), () => WallTests(cf), () => ReferenceCases(cf) };
                    foreach (Action step in steps)
                    {
                        try { step(); }
                        catch (Exception e) { Check(false, c + ": " + e.GetType().Name + ": " + e.Message); }
                    }
                }
            output(string.Format("DoodadCorpusTests: {0} failure(s)", failures));
            return failures;
        }
    }
}
