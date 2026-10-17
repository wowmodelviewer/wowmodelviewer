// DoodadTests.cs
//
// Stage 07 (docs/world-viewer/07-doodads.md): the pure doodad placement layer (Assets/Scripts/Wow/World/DoodadScene.cs) on
// hand-built records -- MDDF de-duplication by uniqueId and the Tile frame with its numbers worked by hand, the doodad-set
// selection (set 0 plus the placement's set, out-of-range sets, ranges past MODD), the composition of a MODD record with a
// moved, turned and scaled world model, the R(q) quaternion convention against its transpose, unsupported records, and
// MODR group membership. Framework-free: RunAll returns the failure count. The checks on real client bytes are in
// DoodadCorpusTests.cs.

using System;
using System.Collections.Generic;
using Wmv.Wow;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class DoodadTests
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

        static DoodadPlacement Mddf(uint name, uint unique, float px, float py, float pz, float a0, float a1, float a2, ushort scale = 1024,
                                    ushort flags = 0x40)
        {
            return new DoodadPlacement
            {
                NameId = name, UniqueId = unique, Position = new WorldFloat3(px, py, pz), Rotation = new WorldFloat3(a0, a1, a2),
                ScaleRaw = scale, Flags = flags,
            };
        }

        static WmoDoodadDef Modd(uint nameIndex, float x, float y, float z, WowQuat q, float scale = 1f)
        {
            return new WmoDoodadDef { NameIndexAndFlags = nameIndex, NameIndex = nameIndex, Position = new WowVec3(x, y, z), Rotation = q, Scale = scale };
        }

        static WowQuat Yaw(double degrees)
        {
            double h = degrees * Math.PI / 360.0;
            return new WowQuat(0f, 0f, (float)Math.Sin(h), (float)Math.Cos(h));
        }

        static WorldPoint Apply(double[] m, double x, double y, double z)
        {
            return new WorldPoint(m[0] * x + m[1] * y + m[2] * z, m[3] * x + m[4] * y + m[5] * z, m[6] * x + m[7] * y + m[8] * z);
        }

        static void MddfTests()
        {
            double h = PlacementTransform.MapHalf;
            var notes = new List<string>();
            var records = new[]
            {
                Mddf(189678, 7, 100f, 20f, 300f, 0f, 90f, 0f, 2048),
                Mddf(189678, 7, 100f, 20f, 300f, 0f, 90f, 0f, 2048),   // the same record in a neighbouring obj0
                Mddf(5000, 8, 10f, 1f, 30f, 0f, 0f, 0f),
                Mddf(5001, 9, 0f, 0f, 0f, 0f, 0f, 0f, 1024, 0x0),        // a name-table entry
                Mddf(0, 10, 0f, 0f, 0f, 0f, 0f, 0f),                     // FileDataID 0
                Mddf(5000, 8, 10f, 1f, 31f, 0f, 0f, 0f),                 // uniqueId 8 again, different contents
            };
            List<WorldDoodad> list = DoodadPlan.FromMddf(records, PlacementFrame.Tile, notes);
            Check(list.Count == 4, "MDDF: one doodad per uniqueId (6 records, 4 uniqueIds)");
            Check(list[0].Records == 2 && list[1].Records == 2 && list[2].Records == 1, "MDDF: repeated records are counted on the first");
            Check(notes.Count == 1 && notes[0].Contains("uniqueId 8"), "MDDF: a repeat with different contents is noted, the first record kept");
            Check(Near(list[1].Position, h - 30.0, h - 10.0, 1.0), "MDDF: the first record of a uniqueId is the one placed");
            WorldDoodad d = list[0];
            Check(d.Source == DoodadSource.Mddf && d.FileDataID == 189678 && d.UniqueId == 7 && d.ModdIndex == -1 && d.Owner == -1 &&
                  d.Unsupported == null, "MDDF: source, FileDataID, uniqueId");
            Check(Near(d.Position, h - 300.0, h - 100.0, 20.0), "MDDF: Tile frame W = (32T - p2, 32T - p0, p1)");
            Check(Near(d.Scale, 2.0), "MDDF: scale u16/1024 (2048 -> 2)");
            // R = Rz(a1 + 180) Ry(a0) Rx(a2) with a1 = 90: Rz(270) takes model +x to world -y.
            Check(Near(Apply(d.Rotation, 1, 0, 0), 0, -1, 0) && Near(Apply(d.Rotation, 0, 1, 0), 1, 0, 0) && Near(Apply(d.Rotation, 0, 0, 1), 0, 0, 1),
                  "MDDF: a1 = 90 is Rz(270): +x -> -y, +y -> +x, z kept");
            Check(Near(d.ToWorld(1, 0, 0), h - 300.0, h - 100.0 - 2.0, 20.0), "MDDF: model point (1, 0, 0) placed at W + 2 * (0, -1, 0)");
            // a1 = 0: Rz(180), the rule's half turn.
            Check(Near(list[1].ToWorld(1, 2, 3), h - 30.0 - 1.0, h - 10.0 - 2.0, 1.0 + 3.0), "MDDF: a1 = 0 is Rz(180): (1, 2, 3) -> (-1, -2, 3) about W");
            // Tilted: a0 = 90 (about world y, after the x roll and before the yaw): model +z -> Rz(180) * (+x) = -x.
            WorldDoodad tilt = DoodadPlan.FromMddf(new[] { Mddf(1, 11, 0f, 0f, 0f, 90f, 0f, 0f) }, PlacementFrame.Tile, null)[0];
            Check(Near(Apply(tilt.Rotation, 0, 0, 1), -1, 0, 0, 1e-12), "MDDF: a0 = 90 tips +z towards -x (Rz(180) Ry(90))");
            Check(list[2].Unsupported != null && list[2].Unsupported.Contains("0x40") && list[2].FileDataID == 0,
                  "MDDF: flag 0x40 clear is unsupported (a name-table entry), FileDataID 0");
            Check(list[3].Unsupported != null && list[3].Unsupported.Contains("FileDataID 0"), "MDDF: FileDataID 0 is unsupported");
            WorldPoint bmin, bmax;
            d.PlaceBox(new WorldPoint(-1, -2, 0), new WorldPoint(1, 2, 4), out bmin, out bmax);
            Check(Near(bmin, h - 300.0 - 4.0, h - 100.0 - 2.0, 20.0) && Near(bmax, h - 300.0 + 4.0, h - 100.0 + 2.0, 28.0),
                  "MDDF: a model box placed (all eight corners, turned and scaled)");
            Check(DoodadPlan.FromMddf(null, PlacementFrame.Tile, null).Count == 0, "MDDF: no records, no doodads");
            WorldDoodad zero = DoodadPlan.FromMddf(new[] { Mddf(7, 12, 0f, 0f, 0f, 0f, 0f, 0f, 0) }, PlacementFrame.Tile, null)[0];
            Check(zero.Unsupported != null && zero.Unsupported.Contains("scale is 0"), "MDDF: scale 0 is unsupported");
        }

        static WmoRoot Root(int moddCount, params WmoDoodadSet[] sets)
        {
            var r = new WmoRoot();
            r.DoodadSets = sets;
            r.DoodadFileDataIDs = new uint[] { 1001, 1002, 0 };
            r.DoodadDefs = new WmoDoodadDef[moddCount];
            for (int k = 0; k < moddCount; k++)
                r.DoodadDefs[k] = Modd((uint)(k % 2), k, 0f, 0f, WowQuat.Identity);
            return r;
        }

        static WmoDoodadSet Set(string name, uint start, uint count) { return new WmoDoodadSet { Name = name, StartIndex = start, Count = count }; }

        static void SelectionTests()
        {
            WmoRoot root = Root(10, Set("Set_$DefaultGlobal", 0, 4), Set("Set_A", 4, 3), Set("Set_B", 7, 3));
            WmoDoodadSelection s0 = DoodadPlan.SelectSets(root, 1, 2, 0);
            Check(s0.Active.Count == 1 && s0.Active[0] == 0 && s0.Records == 4 && s0.Reason == null && s0.SetCount == 3 &&
                  s0.Names[0] == "Set_$DefaultGlobal", "sets: placement set 0 -> set 0 only");
            WmoDoodadSelection s2 = DoodadPlan.SelectSets(root, 1, 2, 2);
            Check(s2.Active.Count == 2 && s2.Active[0] == 0 && s2.Active[1] == 2 && s2.Records == 7 && s2.Reason == null &&
                  s2.Names[1] == "Set_B", "sets: placement set 2 -> set 0 plus set 2 (Set_A not drawn)");
            WmoDoodadSelection s9 = DoodadPlan.SelectSets(root, 1, 2, 9);
            Check(s9.Active.Count == 1 && s9.Records == 4 && s9.Reason != null && s9.Reason.Contains("set 9"),
                  "sets: a set past the root's sets -> set 0 only, with a reason");
            WmoDoodadSelection none = DoodadPlan.SelectSets(new WmoRoot(), 1, 2, 0);
            Check(none.Active.Count == 0 && none.Records == 0 && none.SetCount == 0 && none.Reason != null, "sets: a root without MODS has none");
            WmoRoot past = Root(5, Set("Set_$DefaultGlobal", 0, 4), Set("Set_A", 4, 3));
            WmoDoodadSelection sp = DoodadPlan.SelectSets(past, 1, 2, 1);
            Check(sp.Records == 5 && sp.Reason != null && sp.Reason.Contains("past"), "sets: a range past MODD is clamped to it, with a reason");
            WmoRoot empty0 = Root(3, Set("Set_$DefaultGlobal", 0, 0), Set("Set_All", 0, 3));
            WmoDoodadSelection se = DoodadPlan.SelectSets(empty0, 1, 2, 1);
            Check(se.Active.Count == 2 && se.Records == 3, "sets: an empty set 0 plus a set holding everything (Retail 880031's shape)");
            WmoDoodadSelection all = DoodadPlan.AllSets(root, 1, 2, 2);
            Check(all.Active.Count == 3 && all.Records == 10 && all.Names[1] == "Set_A" && all.Reason != null && all.Reason.StartsWith("DIAGNOSTIC"),
                  "sets: the diagnostic selection draws every set, said");
            Check(DoodadPlan.AllSets(new WmoRoot(), 1, 2, 0).Active.Count == 0, "sets: the diagnostic selection of a root without MODS is empty");
        }

        static void CompositionTests()
        {
            double h = PlacementTransform.MapHalf;
            // A world model at file (100, 20, 300), a1 = 90 (R = Rz(270)), scale 1.
            var rec = new WmoPlacement
            {
                NameId = 892927, UniqueId = 55, Position = new WorldFloat3(100f, 20f, 300f), Rotation = new WorldFloat3(0f, 90f, 0f),
                ExtentsMin = new WorldFloat3(0f, 0f, 0f), ExtentsMax = new WorldFloat3(1f, 1f, 1f), Flags = 0xC, ScaleRaw = 1024, Word58 = 1,
            };
            WorldModelPlacement wmo = WorldModelPlan.Build(new[] { rec }, PlacementFrame.Tile, null)[0];
            var root = new WmoRoot
            {
                DoodadSets = new[] { Set("Set_$DefaultGlobal", 0, 1), Set("Set_A", 1, 4) },
                DoodadFileDataIDs = new uint[] { 189678, 0, 5555 },
                DoodadDefs = new[]
                {
                    Modd(0, 10f, 0f, 0f, WowQuat.Identity, 1f),             // global: on the model's +x axis
                    Modd(2, 0f, 5f, 1f, Yaw(90), 2f),                       // set 1: yawed 90 degrees, scale 2
                    Modd(1, 0f, 0f, 0f, WowQuat.Identity),                  // set 1: MODI entry 1 is 0
                    Modd(7, 0f, 0f, 0f, WowQuat.Identity),                  // set 1: MODI entry 7 does not exist
                    Modd(0, 0f, 0f, 0f, WowQuat.Identity, 0f),              // set 1: scale 0
                },
            };
            WmoDoodadSelection sel = DoodadPlan.SelectSets(root, wmo.UniqueId, wmo.FileDataID, rec.Word58);
            Check(Near(wmo.Scale, 1.0) && wmo.Unsupported == null, "WMO doodads: the world model placement (flags 0xC, scale 1024)");
            List<WorldDoodad> list = DoodadPlan.FromWmo(wmo, 3, root, sel);
            Check(list.Count == 5, "WMO doodads: set 0 (1) plus set 1 (4)");
            WorldDoodad g = list[0];
            Check(g.Source == DoodadSource.WmoSet && g.FileDataID == 189678 && g.UniqueId == 55 && g.WmoFileDataID == 892927 && g.Owner == 3 &&
                  g.ModdIndex == 0 && g.Set == 0 && g.Unsupported == null, "WMO doodads: source, model, owner, MODD index, set");
            // W_d = W + R p: W = (h - 300, h - 100, 20), R = Rz(270) takes (10, 0, 0) to (0, -10, 0).
            Check(Near(g.Position, h - 300.0, h - 100.0 - 10.0, 20.0, 1e-9), "WMO doodads: W_d = W + s R p (Rz(270) * (10, 0, 0) = (0, -10, 0))");
            Check(Near(Apply(g.Rotation, 1, 0, 0), 0, -1, 0, 1e-12), "WMO doodads: identity q -> R_d = R of the world model");
            WorldDoodad y = list[1];
            // p = (0, 5, 1) -> R p = (5, 0, 1); R_d = Rz(270) Rz(90) = identity; s_d = 1 * 2.
            Check(Near(y.Position, h - 300.0 + 5.0, h - 100.0, 21.0, 1e-9), "WMO doodads: a yawed record's position (Rz(270) * (0, 5, 1) = (5, 0, 1))");
            Check(Near(Apply(y.Rotation, 1, 0, 0), 1, 0, 0, 1e-6) && Near(Apply(y.Rotation, 0, 1, 0), 0, 1, 0, 1e-6),
                  "WMO doodads: R_d = R R(q) (Rz(270) Rz(90) = identity)");
            Check(Near(y.Scale, 2.0) && y.FileDataID == 5555 && y.Set == 1, "WMO doodads: s_d = s * MODD scale; MODI names the model");
            Check(Near(y.ToWorld(1, 0, 0), h - 300.0 + 5.0 + 2.0, h - 100.0, 21.0, 1e-6), "WMO doodads: a model point through the composed transform");
            Check(list[2].Unsupported != null && list[2].Unsupported.Contains("which is 0") && list[2].FileDataID == 0, "WMO doodads: MODI entry 0 is unsupported");
            Check(list[3].Unsupported != null && list[3].Unsupported.Contains("past the 3"), "WMO doodads: a MODI index past the table is unsupported");
            Check(list[4].Unsupported != null && list[4].Unsupported.Contains("scale"), "WMO doodads: scale 0 is unsupported");

            // A scaled, tilted world model: W_d = W + s R p, s_d = s s_q.
            var rec2 = rec;
            rec2.Rotation = new WorldFloat3(30f, 45f, 10f);
            rec2.ScaleRaw = 1536;
            WorldModelPlacement wmo2 = WorldModelPlan.Build(new[] { rec2 }, PlacementFrame.Tile, null)[0];
            WorldDoodad t = DoodadPlan.FromWmo(wmo2, 0, root, sel)[1];
            WorldPoint expectPos = wmo2.ToWorld(0, 5, 1);
            Check(Near(t.Position, expectPos.X, expectPos.Y, expectPos.Z, 1e-9) && Near(t.Scale, 1.5 * 2.0), "WMO doodads: a tilted, scaled world model -> W + s R p, s * s_q");
            // A doodad model point lands where the world model's own transform takes the MODD-transformed point.
            double[] q = DoodadPlan.QuaternionMatrix(Yaw(90));
            WorldPoint inner = Apply(q, 2.0 * 1, 2.0 * 2, 2.0 * 3);
            WorldPoint viaWmo = wmo2.ToWorld(0 + inner.X, 5 + inner.Y, 1 + inner.Z);
            Check(Near(t.ToWorld(1, 2, 3), viaWmo.X, viaWmo.Y, viaWmo.Z, 1e-9), "WMO doodads: doodad(x) == wmo(p + s_q R(q) x) for a model point");

            Check(DoodadPlan.FromWmo(null, 0, root, sel).Count == 0 && DoodadPlan.FromWmo(wmo, 0, null, sel).Count == 0,
                  "WMO doodads: nothing without a placement or a root");

            // A record whose position is not a finite number (a damaged file): unsupported, not placed at the scene's origin.
            var damaged = new WmoRoot
            {
                DoodadSets = new[] { Set("Set_$DefaultGlobal", 0, 2) },
                DoodadFileDataIDs = new uint[] { 189678 },
                DoodadDefs = new[] { Modd(0, float.NaN, 0f, 0f, WowQuat.Identity, 1f), Modd(0, 0f, float.PositiveInfinity, 0f, WowQuat.Identity, 1f) },
            };
            List<WorldDoodad> bad = DoodadPlan.FromWmo(wmo, 0, damaged, DoodadPlan.SelectSets(damaged, wmo.UniqueId, wmo.FileDataID, 0));
            Check(bad.Count == 2 && bad[0].Unsupported != null && bad[0].Unsupported.Contains("not a finite number") &&
                  bad[1].Unsupported != null && bad[1].Unsupported.Contains("not a finite number"),
                  "WMO doodads: a position that is not a finite number (NaN, infinity) is unsupported");
            // A stored position finite as a float that the world model's scale (2, turned about z only) takes past float's range.
            var rec3 = rec;
            rec3.ScaleRaw = 2048;
            WorldModelPlacement wmo3 = WorldModelPlan.Build(new[] { rec3 }, PlacementFrame.Tile, null)[0];
            var huge = new WmoRoot
            {
                DoodadSets = new[] { Set("Set_$DefaultGlobal", 0, 1) },
                DoodadFileDataIDs = new uint[] { 189678 },
                DoodadDefs = new[] { Modd(0, 3.0e38f, 0f, 0f, WowQuat.Identity, 1f) },
            };
            List<WorldDoodad> over = DoodadPlan.FromWmo(wmo3, 0, huge, DoodadPlan.SelectSets(huge, wmo3.UniqueId, wmo3.FileDataID, 0));
            Check(over.Count == 1 && over[0].Unsupported != null && over[0].Unsupported.Contains("not a finite number"),
                  "WMO doodads: a composed position past float's range (the instance's position) is unsupported");
        }

        static void QuaternionTests()
        {
            double[] id = DoodadPlan.QuaternionMatrix(WowQuat.Identity);
            Check(Near(Apply(id, 1, 2, 3), 1, 2, 3), "R(q): identity");
            Check(Near(Apply(DoodadPlan.QuaternionMatrix(new WowQuat(0f, 0f, 0f, 0f)), 1, 2, 3), 1, 2, 3), "R(q): a zero quaternion is the identity");
            // q stored x, y, z, w; R(q), not its transpose: +90 about z takes +x to +y (R(q)^T would take it to -y).
            double[] z90 = DoodadPlan.QuaternionMatrix(Yaw(90));
            Check(Near(Apply(z90, 1, 0, 0), 0, 1, 0, 1e-6), "R(q): 90 degrees about z takes +x to +y (the convention, not its transpose)");
            var q2 = Yaw(90);
            var big = new WowQuat(q2.X * 2f, q2.Y * 2f, q2.Z * 2f, q2.W * 2f);
            double[] zb = DoodadPlan.QuaternionMatrix(big);
            Check(Near(Apply(zb, 1, 0, 0), 0, 1, 0, 1e-6), "R(q): normalised first (2q is the same rotation)");
            // 90 degrees about x (stored x first): +y -> +z.
            double s = Math.Sqrt(0.5);
            double[] x90 = DoodadPlan.QuaternionMatrix(new WowQuat((float)s, 0f, 0f, (float)s));
            Check(Near(Apply(x90, 0, 1, 0), 0, 0, 1, 1e-6), "R(q): the first stored component is x (90 degrees about x takes +y to +z)");
            // Orthonormal with determinant +1 for an arbitrary quaternion.
            double[] m = DoodadPlan.QuaternionMatrix(new WowQuat(0.1f, -0.7f, 0.3f, 0.5f));
            double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
            double ortho = 0;
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                {
                    double dot = m[i] * m[j] + m[3 + i] * m[3 + j] + m[6 + i] * m[6 + j];
                    ortho = Math.Max(ortho, Math.Abs(dot - (i == j ? 1 : 0)));
                }
            Check(Near(det, 1.0, 1e-9) && ortho < 1e-9, "R(q): orthonormal, determinant +1");
            Check(Near(Apply(DoodadPlan.QuaternionMatrix(new WowQuat(float.NaN, 0f, 0f, 1f)), 1, 2, 3), 1, 2, 3),
                  "R(q): a non-finite quaternion is the identity");
        }

        static WmoGroup GroupWithModr(params ushort[] modr)
        {
            byte[] header = WmoSynthetic.MogpHeader(0x800, new WowVec3(0f, 0f, 0f), new WowVec3(1f, 1f, 1f), 0, 0, 0);
            byte[] file = WmoSynthetic.Concat(WmoSynthetic.Mver(), WmoSynthetic.Chunk("MOGP", WmoSynthetic.Concat(header, WmoSynthetic.Chunk("MODR", WmoSynthetic.U16s(modr)))));
            return WmoParser.ParseGroup(file, "modr group");
        }

        static void MembershipTests()
        {
            var root = new WmoRoot { DoodadDefs = new WmoDoodadDef[6] };
            WmoGroup a = GroupWithModr(0, 2, 5);
            WmoGroup b = GroupWithModr(2, 3, 3, 40);         // 3 twice, 40 past MODD
            byte[] header = WmoSynthetic.MogpHeader(0, new WowVec3(0f, 0f, 0f), new WowVec3(1f, 1f, 1f), 0, 0, 0);
            WmoGroup none = WmoParser.ParseGroup(WmoSynthetic.Concat(WmoSynthetic.Mver(), WmoSynthetic.Chunk("MOGP", header)), "no modr");
            List<int>[] owners = DoodadPlan.GroupMembership(root, new[] { a, none, null, b });
            Check(owners.Length == 6, "MODR: one entry per MODD record");
            Check(owners[0] != null && owners[0].Count == 1 && owners[0][0] == 0, "MODR: record 0 listed by group 0");
            Check(owners[2] != null && owners[2].Count == 2 && owners[2][0] == 0 && owners[2][1] == 3, "MODR: a record listed by two groups keeps both");
            Check(owners[3] != null && owners[3].Count == 1 && owners[3][0] == 3, "MODR: a record listed twice by one group appears once");
            Check(owners[1] == null && owners[4] == null, "MODR: records no group lists have no owner");
            Check(owners[5] != null && owners[5][0] == 0, "MODR: indices are absolute MODD indices");
            Check(DoodadPlan.GroupMembership(root, null).Length == 6, "MODR: no groups, no owners");
            // Marking takes membership over the SAME groups, as the session does (the groups as built, null where a file could
            // not be read). Every group read: nothing is marked, not even records no group lists.
            Func<List<WorldDoodad>> six = () =>
            {
                var l = new List<WorldDoodad>();
                for (int k = 0; k < 6; k++)
                    l.Add(new WorldDoodad { Source = DoodadSource.WmoSet, ModdIndex = k, FileDataID = 1 });
                return l;
            };
            var all = new[] { a, none, b };
            List<WorldDoodad> doodads = six();
            int marked = DoodadPlan.MarkMissingGroups(doodads, DoodadPlan.GroupMembership(root, all), all);
            Check(marked == 0 && doodads.TrueForAll(d => d.Unsupported == null), "MODR: every group read -- no doodad is marked");
            // Group 3 (which lists records 2 and 3) could not be read: records 1, 3 and 4 -- no read group lists them -- are
            // unsupported, the reason naming the missing groups; records 0, 2 and 5 (group 0 lists them) are kept.
            var built = new[] { a, none, null, null };
            doodads = six();
            marked = DoodadPlan.MarkMissingGroups(doodads, DoodadPlan.GroupMembership(root, built), built);
            Check(marked == 3 && doodads[1].Unsupported != null && doodads[3].Unsupported != null && doodads[4].Unsupported != null &&
                  doodads[3].Unsupported.Contains("group(s) 2, 3 could not be read") &&
                  doodads[0].Unsupported == null && doodads[2].Unsupported == null && doodads[5].Unsupported == null,
                  "MODR: some groups not read -- a doodad no read group lists is unsupported, one a read group lists is kept");
            // A doodad already unsupported keeps its own reason and is not counted again.
            doodads = six();
            doodads[3].Unsupported = "scale 0";
            marked = DoodadPlan.MarkMissingGroups(doodads, DoodadPlan.GroupMembership(root, built), built);
            Check(marked == 2 && doodads[3].Unsupported == "scale 0", "MODR: an unsupported doodad keeps its reason");
        }

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();
            Action[] tests = { MddfTests, SelectionTests, CompositionTests, QuaternionTests, MembershipTests };
            foreach (Action t in tests)
            {
                try { t(); }
                catch (Exception e) { Check(false, t.Method.Name + " threw " + e.GetType().Name + ": " + e.Message); }
            }
            if (output != null)
            {
                foreach (string line in log) output(line);
                output(string.Format("DoodadTests: {0} failure(s)", failures));
            }
            return failures;
        }
    }
}
