// WorldModelTests.cs
//
// Stage 06 (docs/world-viewer/06-world-models-in-world.md): the pure world-model placement layer
// (Assets/Scripts/Wow/World/WorldModelScene.cs and the frames of WorldPlacements.cs) on hand-built records and
// geometry -- the two placement frames, the Unity rotation, de-duplication by uniqueId, the inverse transform, the floor
// and ceiling rays through a tilted, scaled, moved model, and the start point inside a world model. Framework-free:
// RunAll returns the failure count. The checks on real client bytes are in WorldModelCorpusTests.cs.

using System;
using System.Collections.Generic;
using Wmv.Wow;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class WorldModelTests
    {
        static int failures;
        static readonly List<string> log = new List<string>();

        static void Check(bool condition, string name)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name); failures++; }
        }

        static bool Near(double a, double b, double eps = 1e-9) { return Math.Abs(a - b) <= eps; }
        static bool Near(WorldPoint p, double x, double y, double z, double eps = 1e-9)
        {
            return Near(p.X, x, eps) && Near(p.Y, y, eps) && Near(p.Z, z, eps);
        }

        static WmoPlacement Record(uint name, uint unique, float px, float py, float pz, float ra, float rb, float rc, ushort flags = 0xC,
                                   ushort scale = 1024)
        {
            return new WmoPlacement
            {
                NameId = name, UniqueId = unique, Position = new WorldFloat3(px, py, pz), Rotation = new WorldFloat3(ra, rb, rc),
                ExtentsMin = new WorldFloat3(px - 1, py - 1, pz - 1), ExtentsMax = new WorldFloat3(px + 1, py + 1, pz + 1),
                Flags = flags, ScaleRaw = scale,
            };
        }

        static void FrameTests()
        {
            var p = new WorldFloat3(100f, 20f, 300f);
            WorldPoint tile = PlacementTransform.ToWorld(p, PlacementFrame.Tile);
            WorldPoint old = PlacementTransform.ToWorld(p);
            Check(Near(tile, old.X, old.Y, old.Z), "the Tile frame is the audit's rule unchanged");
            Check(Near(tile, PlacementTransform.MapHalf - 300.0, PlacementTransform.MapHalf - 100.0, 20.0), "Tile frame: (32T - p[2], 32T - p[0], p[1])");
            WorldPoint global = PlacementTransform.ToWorld(p, PlacementFrame.Global);
            Check(Near(global, -300.0, -100.0, 20.0), "Global frame: (-p[2], -p[0], p[1]), no map-centre offset");
            Check(Near(PlacementTransform.ToWorld(new WorldFloat3(0f, 0f, 0f), PlacementFrame.Global), 0.0, 0.0, 0.0),
                  "Global frame: the modern-form position (0, 0, 0) is the world origin");
            WorldPoint mn, mx, omn, omx;
            var emin = new WorldFloat3(-151.8212f, -35.5493f, -197.2988f);
            var emax = new WorldFloat3(150.3298f, 15.2167f, 8.1363f);
            PlacementTransform.ExtentsToWorld(emin, emax, PlacementFrame.Global, out mn, out mx);
            Check(Near(mn, -8.1363, -150.3298, -35.5493, 1e-4) && Near(mx, 197.2988, 151.8212, 15.2167, 1e-4),
                  "Global extents: X from -max[2] to -min[2], Y from -max[0] to -min[0], Z from min[1] to max[1] (map 34's record)");
            PlacementTransform.ExtentsToWorld(emin, emax, PlacementFrame.Tile, out mn, out mx);
            PlacementTransform.ExtentsToWorld(emin, emax, out omn, out omx);
            Check(Near(mn, omn.X, omn.Y, omn.Z) && Near(mx, omx.X, omx.Y, omx.Z), "Tile extents: the audit's rule unchanged");
            // The placed box in each frame differs by exactly the offset.
            var rot = new WorldFloat3(3f, 40f, -7f);
            WorldPoint a0, a1, b0, b1;
            PlacementTransform.PlacedBox(p, rot, 1.5, new WorldPoint(-2, -3, -4), new WorldPoint(5, 6, 7), PlacementFrame.Tile, out a0, out a1);
            PlacementTransform.PlacedBox(p, rot, 1.5, new WorldPoint(-2, -3, -4), new WorldPoint(5, 6, 7), PlacementFrame.Global, out b0, out b1);
            double h = PlacementTransform.MapHalf;
            Check(Near(a0, b0.X + h, b0.Y + h, b0.Z, 1e-6) && Near(a1, b1.X + h, b1.Y + h, b1.Z, 1e-6), "the two frames differ by the offset alone");
        }

        static void UnityRotationTests()
        {
            var rnd = new Random(6);
            var angles = new List<WorldFloat3>
            {
                new WorldFloat3(0f, 0f, 0f), new WorldFloat3(0f, 180f, 0f), new WorldFloat3(0f, 38.5f, 0f), new WorldFloat3(12f, 281f, -4f),
                new WorldFloat3(-90f, 45f, 30f), new WorldFloat3(7.5f, 133.78f, 2.25f),
            };
            for (int i = 0; i < 20; i++)
                angles.Add(new WorldFloat3((float)(rnd.NextDouble() * 360 - 180), (float)(rnd.NextDouble() * 360), (float)(rnd.NextDouble() * 360 - 180)));
            bool det = true, maps = true;
            foreach (WorldFloat3 a in angles)
            {
                double[] r = PlacementTransform.Rotation(a);
                double[] u = PlacementTransform.UnityRotation(r);
                det &= Near(PlacementTransform.Determinant(u), 1.0, 1e-9) && Near(PlacementTransform.Determinant(r), 1.0, 1e-9);
                // A model point m: the builder's vertex is M m = (-m.y, m.z, m.x); turned by U it must be M (R m).
                for (int k = 0; k < 4; k++)
                {
                    double mx = rnd.NextDouble() * 20 - 10, my = rnd.NextDouble() * 20 - 10, mz = rnd.NextDouble() * 20 - 10;
                    double rx = r[0] * mx + r[1] * my + r[2] * mz, ry = r[3] * mx + r[4] * my + r[5] * mz, rz = r[6] * mx + r[7] * my + r[8] * mz;
                    double cx = -my, cy = mz, cz = mx;   // M m
                    double ux = u[0] * cx + u[1] * cy + u[2] * cz, uy = u[3] * cx + u[4] * cy + u[5] * cz, uz = u[6] * cx + u[7] * cy + u[8] * cz;
                    maps &= Near(ux, -ry, 1e-9) && Near(uy, rz, 1e-9) && Near(uz, rx, 1e-9);
                }
            }
            Check(det, "R and U = M R M^T are proper rotations (det +1) for 26 angle sets");
            Check(maps, "U turns a converted vertex (M m) to the converted placed vertex (M R m), 104 points");
        }

        static void PlanTests()
        {
            var notes = new List<string>();
            var recs = new List<WmoPlacement>
            {
                Record(1001, 7, 100, 10, 200, 0, 30, 0),
                Record(1002, 8, 110, 12, 210, 0, 0, 0),
                Record(1001, 7, 100, 10, 200, 0, 30, 0),           // a repeat (another tile's obj0)
                Record(1001, 7, 100, 10, 201, 0, 30, 0),           // same uniqueId, a different record
                Record(5, 9, 0, 0, 0, 0, 0, 0, 0, 0),              // the old WDT form: flag 0x8 clear
                Record(0, 10, 0, 0, 0, 0, 0, 0),                   // FileDataID 0
            };
            List<WorldModelPlacement> plan = WorldModelPlan.Build(recs, PlacementFrame.Tile, notes);
            Check(plan.Count == 4, "one placement per uniqueId (4 of 6 records)");
            Check(plan[0].Records == 3 && plan[1].Records == 1, "repeats of a uniqueId are counted on its placement");
            Check(notes.Count == 1 && notes[0].Contains("uniqueId 7"), "a differing repeat is noted, the first record placed");
            Check(plan[0].FileDataID == 1001 && plan[0].Unsupported == null, "a FileDataID name is placed");
            Check(plan[2].Unsupported != null && plan[2].FileDataID == 0 && plan[2].Unsupported.Contains("MWMO"),
                  "an MWMO-named record is unsupported, with why");
            Check(plan[3].Unsupported != null && plan[3].Unsupported.Contains("FileDataID 0"), "a record naming FileDataID 0 is unsupported");
            Check(Near(plan[0].Position, PlacementTransform.MapHalf - 200, PlacementTransform.MapHalf - 100, 10, 1e-6),
                  "the placement's position is the frame's world point");
            List<WorldModelPlacement> g = WorldModelPlan.Build(new[] { Record(108631, 0xFFFFFFFF, 0, 0, 0, 0, 0, 0, 0x8, 1024) },
                                                               PlacementFrame.Global, null);
            Check(g.Count == 1 && Near(g[0].Position, 0, 0, 0) && g[0].Scale == 1.0, "a global record at (0, 0, 0) stands at the world origin, scale 1");
        }

        static void InverseTests()
        {
            var rnd = new Random(60);
            bool ok = true;
            for (int i = 0; i < 50; i++)
            {
                WmoPlacement r = Record(1, (uint)i, (float)rnd.NextDouble() * 34000, (float)rnd.NextDouble() * 200, (float)rnd.NextDouble() * 34000,
                                        (float)rnd.NextDouble() * 40 - 20, (float)rnd.NextDouble() * 360, (float)rnd.NextDouble() * 40 - 20, 0xC,
                                        (ushort)(512 + rnd.Next(2048)));
                WorldModelPlacement p = WorldModelPlan.Build(new[] { r }, PlacementFrame.Tile, null)[0];
                double x = rnd.NextDouble() * 200 - 100, y = rnd.NextDouble() * 200 - 100, z = rnd.NextDouble() * 50;
                WorldPoint w = p.ToWorld(x, y, z);
                WorldPoint back = p.ToModel(w.X, w.Y, w.Z);
                ok &= Near(back, x, y, z, 1e-6);
                WorldPoint viaTransform = PlacementTransform.Transform(r.Position, r.Rotation, r.Scale, new WorldPoint(x, y, z), PlacementFrame.Tile);
                ok &= Near(w, viaTransform.X, viaTransform.Y, viaTransform.Z, 1e-6);
            }
            Check(ok, "ToWorld agrees with PlacementTransform.Transform and ToModel inverts it (50 tilted, scaled placements)");
        }

        // A box room: floor (z = 0) and ceiling (z = 5) as two quads each, 20 x 10 yards in x and y; flags as given.
        static WmoGroup Room(float x0, float y0, float x1, float y1, float floor, float ceiling, uint flags)
        {
            var g = new WmoGroup();
            g.Positions = new[]
            {
                new WowVec3(x0, y0, floor), new WowVec3(x1, y0, floor), new WowVec3(x1, y1, floor), new WowVec3(x0, y1, floor),
                new WowVec3(x0, y0, ceiling), new WowVec3(x1, y0, ceiling), new WowVec3(x1, y1, ceiling), new WowVec3(x0, y1, ceiling),
            };
            g.Indices = new ushort[] { 0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6 };
            var h = new WmoGroupHeader { Flags = flags, BoundsMin = new WowVec3(x0, y0, floor), BoundsMax = new WowVec3(x1, y1, ceiling) };
            g.Header = h;
            return g;
        }

        // One up-facing quad (a floor layer, no ceiling) over x0..x1, y0..y1 at height z.
        static WmoGroup Layer(float x0, float y0, float x1, float y1, float z)
        {
            var g = new WmoGroup();
            g.Positions = new[] { new WowVec3(x0, y0, z), new WowVec3(x1, y0, z), new WowVec3(x1, y1, z), new WowVec3(x0, y1, z) };
            g.Indices = new ushort[] { 0, 1, 2, 0, 2, 3 };
            g.Header = new WmoGroupHeader { Flags = 0x2000, BoundsMin = new WowVec3(x0, y0, z), BoundsMax = new WowVec3(x1, y1, z) };
            return g;
        }

        static void GeometryTests()
        {
            // Placed tilted 10 degrees about x, turned, scaled 2, far from the origin: the queries go through the transform.
            WmoPlacement rec = Record(1, 1, 1000, 50, 2000, 10f, 70f, 0f, 0xC, 2048);
            WorldModelPlacement p = WorldModelPlan.Build(new[] { rec }, PlacementFrame.Tile, null)[0];
            var groups = new[] { Room(-10, -5, 10, 5, 0, 5, 0x2000), Room(20, -5, 40, 5, 2, 3.2f, 0x2000), Room(-50, -50, 50, 50, -1, -1, 0x8) };
            var geo = new WorldModelGeometry(p, null, groups);
            // A point of room 0's floor, in the world.
            WorldPoint f = p.ToWorld(1.0, 1.0, 0.0);
            WorldPoint c = p.ToWorld(1.0, 1.0, 5.0);
            double floorZ, ceilZ;
            int group;
            // From above the ceiling, down: the ceiling is met first (the highest surface below the start).
            bool hit = geo.FloorBelow(f.X, f.Y, c.Z + 50.0, out floorZ, out group);
            WorldPoint expectTop = Surface(p, f.X, f.Y, 5.0);
            Check(hit && group == 0 && Near(floorZ, expectTop.Z, 1e-6), "from above, the first surface down is room 0's roof, through a tilted, scaled placement");
            // From inside the room, down: the floor; up: the ceiling.
            WorldPoint mid = p.ToWorld(1.0, 1.0, 2.0);
            hit = geo.FloorBelow(mid.X, mid.Y, mid.Z, out floorZ, out group);
            WorldPoint expectFloor = Surface(p, mid.X, mid.Y, 0.0);
            Check(hit && group == 0 && Near(floorZ, expectFloor.Z, 1e-6), "inside the room, the floor under the point (world z exact)");
            bool up = geo.CeilingAbove(mid.X, mid.Y, mid.Z, out ceilZ);
            WorldPoint expectCeil = Surface(p, mid.X, mid.Y, 5.0);
            Check(up && Near(ceilZ, expectCeil.Z, 1e-6), "inside the room, the ceiling above the point");
            // Outside every group: nothing.
            WorldPoint far = p.ToWorld(500.0, 500.0, 0.0);
            Check(!geo.FloorBelow(far.X, far.Y, far.Z + 100, out floorZ, out group), "no floor outside every group");
            // The start point: interior groups only (group 2, the large exterior plane, is not one), the largest floor with
            // headroom: room 0 (200 sq yd, 5 high) over room 1 (200 sq yd, 1.2 high: too low).
            WorldPoint start;
            double yaw;
            string why;
            bool found = geo.StartPoint(2.0, out start, out yaw, out group, out why);
            Check(found && group == 0, "the start point is in the interior room with the largest floor that has headroom (" + why + ")");
            hit = geo.FloorBelow(start.X, start.Y, start.Z, out floorZ, out int g2);
            Check(hit && g2 == 0 && start.Z - floorZ > 1.5 && start.Z - floorZ < 4.5, "the start stands above room 0's floor at about the eye height (scale 2)");
            Check(geo.CeilingAbove(start.X, start.Y, start.Z, out ceilZ) && ceilZ > start.Z, "the start is under room 0's ceiling");
            // StandOn: from above, the roof's underside and top are not floors to stand on; the room's floor is, covered by its
            // ceiling. Two storeys at the same place: the higher covered one.
            double sz, sf;
            string sr;
            bool stood = geo.StandOn(mid.X, mid.Y, 2.0, out sz, out sf, out sr);
            Check(stood && Near(sf, expectFloor.Z, 1e-6) && Near(sz, sf + 2.0, 1e-9) && sr.Contains("covered"),
                  "StandOn: on room 0's covered floor, not on its roof (" + sr + ")");
            var storeys = new WorldModelGeometry(p, null, new[] { Room(-10, -5, 10, 5, 0, 5, 0x2000), Room(-10, -5, 10, 5, 6, 11, 0x2000) });
            WorldPoint mid2 = p.ToWorld(1.0, 1.0, 8.0);
            WorldPoint upper = Surface(p, mid2.X, mid2.Y, 6.0);
            stood = storeys.StandOn(mid2.X, mid2.Y, 2.0, out sz, out sf, out sr);
            Check(stood && Near(sf, upper.Z, 1e-6), "StandOn: two storeys -- the higher covered floor");
            Check(!geo.StandOn(far.X, far.Y, 2.0, out sz, out sf, out sr) && sr.Contains("no floor"), "StandOn: no floor outside every group: said");
            // A floor limit (a world model on a terrain tile: nothing under the terrain's ground): room 0's floor is not stood on
            // when the limit is above it; below it, the same covered floor as without one.
            bool coveredFloor;
            stood = geo.StandOn(mid.X, mid.Y, 2.0, expectFloor.Z + 1.0, out sz, out sf, out coveredFloor, out sr);
            Check(!stood || sf >= expectFloor.Z + 1.0, "StandOn with a floor limit: no floor under the limit is stood on (" + sr + ")");
            stood = geo.StandOn(mid.X, mid.Y, 2.0, expectFloor.Z - 1.0, out sz, out sf, out coveredFloor, out sr);
            Check(stood && coveredFloor && Near(sf, expectFloor.Z, 1e-6), "StandOn with a limit under the floor: the covered floor as before (" + sr + ")");
            // GroundFloor (a world model on a terrain tile): the LOWEST floor at or above the limit -- two storeys: the lower one
            // with the limit under it, the upper one with the limit between them; nothing with the limit above both.
            double lower = Surface(p, mid2.X, mid2.Y, 0.0).Z;
            stood = storeys.GroundFloor(mid2.X, mid2.Y, 2.0, lower - 0.5, out sz, out sf, out coveredFloor, out sr);
            Check(stood && Near(sf, lower, 1e-6) && coveredFloor, "GroundFloor: two storeys -- the lower floor, covered (" + sr + ")");
            stood = storeys.GroundFloor(mid2.X, mid2.Y, 2.0, (lower + upper.Z) / 2.0, out sz, out sf, out coveredFloor, out sr);
            Check(stood && Near(sf, upper.Z, 1e-6), "GroundFloor: the limit between the storeys -- the upper floor (" + sr + ")");
            Check(!storeys.GroundFloor(mid2.X, mid2.Y, 2.0, upper.Z + 20.0, out sz, out sf, out coveredFloor, out sr),
                  "GroundFloor: the limit above every floor -- none (" + sr + ")");
            // A floor laid in layers (scale 2: 0.3 and 0.55 model units are 0.6 and 1.1 yards up): the point stands on the
            // stack's top, its eye above every layer; a layer more than eye + 0.5 above (1.5 model units, 3 yards) is not
            // climbed onto.
            var stack = new WorldModelGeometry(p, null, new[] { Room(-10, -5, 10, 5, 0, 5, 0x2000), Layer(-10, -5, 10, 5, 0.3f),
                                                                 Layer(-10, -5, 10, 5, 0.55f) });
            double layerTop = Surface(p, mid.X, mid.Y, 0.55).Z;
            stood = stack.GroundFloor(mid.X, mid.Y, 2.0, expectFloor.Z - 0.5, out sz, out sf, out coveredFloor, out sr);
            Check(stood && Near(sf, layerTop, 1e-6) && Near(sz, layerTop + 2.0, 1e-6) && sr.Contains("3 floor layers"),
                  "GroundFloor: a floor laid in three layers -- the top one, the eye 2 yards above it (" + sr + ")");
            Check(stack.FloorAbove(mid.X, mid.Y, expectFloor.Z + 0.05, out ceilZ) && Near(ceilZ, Surface(p, mid.X, mid.Y, 0.3).Z, 1e-6),
                  "FloorAbove: the next up-facing layer, not the ceiling's down-facing surface");
            var high = new WorldModelGeometry(p, null, new[] { Room(-10, -5, 10, 5, 0, 5, 0x2000), Layer(-10, -5, 10, 5, 1.5f) });
            stood = high.GroundFloor(mid.X, mid.Y, 2.0, expectFloor.Z - 0.5, out sz, out sf, out coveredFloor, out sr);
            Check(stood && Near(sf, expectFloor.Z, 1e-6) && sz <= Surface(p, mid.X, mid.Y, 1.5).Z - 0.5 + 1e-6,
                  "GroundFloor: a floor 3 yards up is not climbed; the eye stays half a yard under it (" + sr + ")");
            // Only a low room: no start.
            var low = new WorldModelGeometry(p, null, new[] { Room(20, -5, 40, 5, 2, 2.6f, 0x2000) });
            Check(!low.StartPoint(2.0, out start, out yaw, out group, out why) && why.Contains("headroom"), "no start without 2.5 yards of headroom: said");
            Check(!new WorldModelGeometry(p, null, new WmoGroup[0]).StartPoint(2.0, out start, out yaw, out group, out why), "no group: no start");
        }

        /// <summary>The world z where the plane z_model = h of placement p meets the vertical line (x, y): solved exactly.</summary>
        static WorldPoint Surface(WorldModelPlacement p, double x, double y, double h)
        {
            // model = R^T (world - W) / s; z_model = h along the vertical line world = (x, y, t): linear in t.
            WorldPoint a = p.ToModel(x, y, 0.0), b = p.ToModel(x, y, 1.0);
            double t = (h - a.Z) / (b.Z - a.Z);
            return new WorldPoint(x, y, t);
        }

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();
            log.Add("Placement frames");
            FrameTests();
            log.Add("Unity rotation");
            UnityRotationTests();
            log.Add("Placement plan");
            PlanTests();
            log.Add("Inverse transform");
            InverseTests();
            log.Add("Floor, ceiling and start point");
            GeometryTests();
            log.Add(string.Format("WorldModelTests: {0} failure(s)", failures));
            if (output != null)
                foreach (string line in log)
                    output(line);
            return failures;
        }
    }
}
