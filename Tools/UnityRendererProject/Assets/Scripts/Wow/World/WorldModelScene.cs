// WorldModelScene.cs
//
// WORLD MODELS IN THE WORLD (stage 06, docs/world-viewer/06-world-models-in-world.md): the pure part of placing WMOs in
// the world scene -- which placements a load has (MODF records de-duplicated by uniqueId), where each one stands (the
// proven transform, WorldPlacements.cs, in the record's own frame), the world boxes of a placed WMO's groups (the Maps
// viewer's footprint and the group culling), the floor under a point (the free camera's ground inside a world model), and
// where a camera may start inside a world model when no target is given. Pure C#, no UnityEngine: the parser tests run it
// outside the editor (Tests/WorldModelTests.cs, Tests/WorldModelCorpusTests.cs).
//
// Every geometric query works on the parsed group files (WmoParser) in the WMO's own model space and maps through the
// placement: world = W + scale * R * model. A vertical ray in the world is the ray R^T * (0, 0, -1) in model space, so
// a tilted placement is handled exactly, not approximated.

using System;
using System.Collections.Generic;

namespace Wmv.Wow.World
{
    /// <summary>One world-model placement of a scene: a MODF record (the first of its uniqueId) and where it stands.</summary>
    public sealed class WorldModelPlacement
    {
        public WmoPlacement Record;
        public PlacementFrame Frame;
        public uint UniqueId;
        /// <summary>The WMO root's FileDataID; 0 when the record does not name one by FileDataID.</summary>
        public uint FileDataID;
        /// <summary>Records of the source with this uniqueId (a placement repeated in several obj0 files is one instance).</summary>
        public int Records = 1;
        /// <summary>W: the placement's world position (game frame).</summary>
        public WorldPoint Position;
        /// <summary>R: row-major 3x3, world axes (PlacementTransform.Rotation).</summary>
        public double[] Rotation;
        public double Scale = 1.0;
        /// <summary>The record's extents as the world box they are (PlacementTransform.ExtentsToWorld).</summary>
        public WorldPoint ExtentsMin, ExtentsMax;
        /// <summary>Why this placement cannot be placed (the record names no FileDataID), or null.</summary>
        public string Unsupported;

        /// <summary>A model-space point placed in the world.</summary>
        public WorldPoint ToWorld(double x, double y, double z)
        {
            double[] m = Rotation;
            double sx = Scale * x, sy = Scale * y, sz = Scale * z;
            return new WorldPoint(Position.X + m[0] * sx + m[1] * sy + m[2] * sz,
                                  Position.Y + m[3] * sx + m[4] * sy + m[5] * sz,
                                  Position.Z + m[6] * sx + m[7] * sy + m[8] * sz);
        }

        /// <summary>A world point in the model's own space (the inverse of ToWorld: R is orthonormal, so R^-1 = R^T).</summary>
        public WorldPoint ToModel(double x, double y, double z)
        {
            double[] m = Rotation;
            double dx = x - Position.X, dy = y - Position.Y, dz = z - Position.Z;
            double s = Scale != 0.0 ? 1.0 / Scale : 0.0;
            return new WorldPoint(s * (m[0] * dx + m[3] * dy + m[6] * dz), s * (m[1] * dx + m[4] * dy + m[7] * dz),
                                  s * (m[2] * dx + m[5] * dy + m[8] * dz));
        }

        /// <summary>A world direction in the model's space (no scale, no translation).</summary>
        public WorldPoint DirectionToModel(double x, double y, double z)
        {
            double[] m = Rotation;
            return new WorldPoint(m[0] * x + m[3] * y + m[6] * z, m[1] * x + m[4] * y + m[7] * z, m[2] * x + m[5] * y + m[8] * z);
        }

        /// <summary>A model-space box placed in the world (all eight corners).</summary>
        public void PlaceBox(WowVec3 min, WowVec3 max, out WorldPoint wmin, out WorldPoint wmax)
        {
            wmin = new WorldPoint(double.MaxValue, double.MaxValue, double.MaxValue);
            wmax = new WorldPoint(double.MinValue, double.MinValue, double.MinValue);
            for (int k = 0; k < 8; k++)
            {
                WorldPoint q = ToWorld((k & 1) == 0 ? min.X : max.X, (k & 2) == 0 ? min.Y : max.Y, (k & 4) == 0 ? min.Z : max.Z);
                wmin = new WorldPoint(Math.Min(wmin.X, q.X), Math.Min(wmin.Y, q.Y), Math.Min(wmin.Z, q.Z));
                wmax = new WorldPoint(Math.Max(wmax.X, q.X), Math.Max(wmax.Y, q.Y), Math.Max(wmax.Z, q.Z));
            }
        }

        public override string ToString()
        {
            return string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                 "uniqueId {0} WMO {1} at {2} rotation ({3}, {4}, {5}) scale {6:0.###} ({7} frame){8}", UniqueId, FileDataID,
                                 Position, Record.Rotation.A, Record.Rotation.B, Record.Rotation.C, Scale, Frame,
                                 Unsupported != null ? " -- unsupported: " + Unsupported : "");
        }
    }

    /// <summary>A placed world model's group, as the world sees it: its box (the MOGI box placed) and its flags.</summary>
    public struct WorldModelGroupBox
    {
        public int Group;
        public uint Flags;                 // MOGI flags (as stored)
        public WorldPoint Min, Max;        // world frame
        /// <summary>MOGI/MOGP flag 0x2000: an interior group (WmoGroupFlags; drawn and counted as stored, not interpreted
        /// further here).</summary>
        public bool Interior { get { return (Flags & 0x2000) != 0; } }
    }

    /// <summary>A floor found under a point.</summary>
    public struct WorldModelFloor
    {
        public bool Found;
        public double Z;                   // world height of the floor
        public int Placement;              // the placement's index in the list searched, -1 for none
        public int Group;                  // the group's index in its WMO, -1 for none
    }

    public static class WorldModelPlan
    {
        /// <summary>
        /// The placements of a scene from its MODF records (one obj0's, or the WDT's): one per uniqueId, the first record
        /// kept and the others counted (a record repeated with any difference is noted -- never observed by the audit);
        /// each with its world transform in `frame`. A record that names no FileDataID (the old WDT form: an MWMO entry)
        /// is kept with Unsupported set: it is counted, never guessed at.
        /// </summary>
        public static List<WorldModelPlacement> Build(IList<WmoPlacement> records, PlacementFrame frame, List<string> notes)
        {
            var list = new List<WorldModelPlacement>();
            var byId = new Dictionary<uint, WorldModelPlacement>();
            if (records == null)
                return list;
            for (int i = 0; i < records.Count; i++)
            {
                WmoPlacement r = records[i];
                WorldModelPlacement seen;
                if (byId.TryGetValue(r.UniqueId, out seen))
                {
                    seen.Records++;
                    if (!seen.Record.SamePlacementAs(r) && notes != null)
                        notes.Add(string.Format("uniqueId {0}: record {1} differs from the first record of that uniqueId -- the first is placed",
                                                r.UniqueId, i));
                    continue;
                }
                var p = new WorldModelPlacement
                {
                    Record = r, Frame = frame, UniqueId = r.UniqueId, Scale = r.Scale,
                    Position = PlacementTransform.ToWorld(r.Position, frame),
                    Rotation = PlacementTransform.Rotation(r.Rotation),
                };
                PlacementTransform.ExtentsToWorld(r.ExtentsMin, r.ExtentsMax, frame, out p.ExtentsMin, out p.ExtentsMax);
                if (r.NameKind != PlacementNameKind.FileDataId)
                    p.Unsupported = "the record names its world model by an MWMO entry (the old WDT form), not by FileDataID";
                else if (r.NameId == 0)
                    p.Unsupported = "the record names FileDataID 0";
                else if (r.NameId > int.MaxValue)
                    p.Unsupported = "the record names FileDataID " + r.NameId + ", beyond what a request can carry";
                else
                    p.FileDataID = r.NameId;
                byId[r.UniqueId] = p;
                list.Add(p);
            }
            return list;
        }

        /// <summary>Every group's MOGI box placed in the world (the footprint), in MOGI order.</summary>
        public static WorldModelGroupBox[] GroupBoxes(WmoRoot root, WorldModelPlacement p)
        {
            var boxes = new WorldModelGroupBox[root.GroupInfos.Length];
            for (int i = 0; i < boxes.Length; i++)
            {
                WmoGroupInfo g = root.GroupInfos[i];
                WorldPoint min, max;
                p.PlaceBox(g.BoundsMin, g.BoundsMax, out min, out max);
                boxes[i] = new WorldModelGroupBox { Group = i, Flags = g.Flags, Min = min, Max = max };
            }
            return boxes;
        }

        /// <summary>The root box (MOHD) placed in the world.</summary>
        public static void RootBox(WmoRoot root, WorldModelPlacement p, out WorldPoint min, out WorldPoint max)
        {
            p.PlaceBox(root.Header.BoundsMin, root.Header.BoundsMax, out min, out max);
        }

        /// <summary>The textures the drawn materials of a world model sample, once each: the materials some LOD0 batch with
        /// triangles uses, through the same plan the builder draws from (WmoMaterialSemantics.CollectSampledTextures) -- the
        /// Buildings decode policy, so what is fetched and what is drawn cannot drift apart.</summary>
        public static HashSet<uint> SampledTextures(WmoRoot root, WmoGroup[] groups)
        {
            var used = new HashSet<int>();
            if (groups != null)
                foreach (WmoGroup g in groups)
                {
                    if (g == null) continue;
                    foreach (WmoBatch b in g.Batches)
                        if (b.TriangleIndexCount > 0 && b.MaterialId >= 0 && b.MaterialId < root.Materials.Length)
                            used.Add(b.MaterialId);
                }
            var sampled = new HashSet<uint>();
            foreach (int id in used)
                WmoMaterialSemantics.CollectSampledTextures(WmoMaterialSemantics.Plan(root.Materials[id]), sampled);
            sampled.Remove(0);
            return sampled;
        }
    }

    /// <summary>A world model placed with its parsed groups: what the floor and start-point queries search.</summary>
    public sealed class WorldModelGeometry
    {
        public WorldModelPlacement Placement;
        public WmoRoot Root;
        public WmoGroup[] Groups;          // LOD0, GFID order; null entries are missing files

        public WorldModelGeometry(WorldModelPlacement placement, WmoRoot root, WmoGroup[] groups)
        {
            Placement = placement;
            Root = root;
            Groups = groups ?? new WmoGroup[0];
        }

        /// <summary>
        /// The highest surface of this world model under (x, y) at or below zFrom (world frame): a ray cast down from
        /// (x, y, zFrom) through every triangle of every group whose box the ray meets. Every triangle of MOVI counts --
        /// render and collision triangles alike: no MOPY rule is assumed. False when the ray meets none.
        /// </summary>
        public bool FloorBelow(double x, double y, double zFrom, out double floorZ, out int group)
        {
            return FloorBelow(x, y, zFrom, false, out floorZ, out group);
        }

        /// <summary>FloorBelow; with frontOnly, only a triangle met from its front -- up-facing by the file's winding, a floor
        /// seen from above, never the underside of a roof or a floor above (FloorTriangles' rule).</summary>
        public bool FloorBelow(double x, double y, double zFrom, bool frontOnly, out double floorZ, out int group)
        {
            floorZ = double.NaN;
            group = -1;
            WorldModelPlacement p = Placement;
            WorldPoint o = p.ToModel(x, y, zFrom);
            WorldPoint d = p.DirectionToModel(0.0, 0.0, -1.0);
            double best = double.MaxValue;   // the ray parameter (model units) of the nearest hit
            for (int gi = 0; gi < Groups.Length; gi++)
            {
                WmoGroup g = Groups[gi];
                if (g == null || g.Positions.Length == 0 || g.Indices.Length < 3)
                    continue;
                if (!RayMeetsBox(o, d, g.Header.BoundsMin, g.Header.BoundsMax))
                    continue;
                double t;
                if (NearestHit(g, o, d, best, frontOnly, out t))
                {
                    best = t;
                    group = gi;
                }
            }
            if (group < 0)
                return false;
            // t is in model units along a unit direction; the world distance is scale * t, straight down.
            floorZ = zFrom - p.Scale * best;
            return true;
        }

        /// <summary>The nearest surface ABOVE (x, y, zFrom), or false: the room's ceiling, when there is one.</summary>
        public bool CeilingAbove(double x, double y, double zFrom, out double ceilingZ)
        {
            return SurfaceAbove(x, y, zFrom, false, out ceilingZ);
        }

        /// <summary>The nearest UP-FACING surface above (x, y, zFrom) -- a floor seen from below (FloorTriangles' rule) --, or
        /// false.</summary>
        public bool FloorAbove(double x, double y, double zFrom, out double floorZ)
        {
            return SurfaceAbove(x, y, zFrom, true, out floorZ);
        }

        bool SurfaceAbove(double x, double y, double zFrom, bool upFacingOnly, out double ceilingZ)
        {
            ceilingZ = double.NaN;
            WorldModelPlacement p = Placement;
            WorldPoint o = p.ToModel(x, y, zFrom);
            WorldPoint d = p.DirectionToModel(0.0, 0.0, 1.0);
            double best = double.MaxValue;
            bool any = false;
            for (int gi = 0; gi < Groups.Length; gi++)
            {
                WmoGroup g = Groups[gi];
                if (g == null || g.Positions.Length == 0 || g.Indices.Length < 3 || !RayMeetsBox(o, d, g.Header.BoundsMin, g.Header.BoundsMax))
                    continue;
                double t;
                if (NearestHit(g, o, d, best, false, out t, upFacingOnly))
                {
                    best = t;
                    any = true;
                }
            }
            if (!any)
                return false;
            ceilingZ = zFrom + p.Scale * best;
            return true;
        }

        /// <summary>
        /// Where to stand at (x, y) in this world model (world frame): every floor under the point from above the model's top,
        /// top down (FloorBelow, front faces only), and the highest one that is COVERED -- a surface above it at least 2 yards
        /// up: inside the model, not on its roof. With no covered floor, the highest floor (said in the reason). False with
        /// the reason when no floor is under the point. z is the floor plus `eye` (less under a low ceiling).
        /// </summary>
        public bool StandOn(double x, double y, double eye, out double z, out double floorZ, out string reason)
        {
            bool covered;
            return StandOn(x, y, eye, double.NegativeInfinity, out z, out floorZ, out covered, out reason);
        }

        /// <summary>
        /// StandOn, with no floor under `minFloor` stood on (a world model on a terrain tile: a floor under the terrain's
        /// ground there -- a cellar, a crypt -- is not where a point of the outdoor map stands); `covered` says whether the
        /// floor chosen has a surface above it.
        /// </summary>
        public bool StandOn(double x, double y, double eye, double minFloor, out double z, out double floorZ, out bool covered, out string reason)
        {
            z = floorZ = double.NaN;
            covered = false;
            // Above everything: the placed root box, the record's extents and every group's placed box.
            double top = Placement.ExtentsMax.Z;
            WorldPoint bmin, bmax;
            if (Root != null)
            {
                WorldModelPlan.RootBox(Root, Placement, out bmin, out bmax);
                top = Math.Max(top, bmax.Z);
            }
            foreach (WmoGroup g in Groups)
                if (g != null)
                {
                    Placement.PlaceBox(g.Header.BoundsMin, g.Header.BoundsMax, out bmin, out bmax);
                    top = Math.Max(top, bmax.Z);
                }
            double from = top + 1.0, highest = double.NaN;
            for (int k = 0; k < 256; k++)
            {
                double f;
                int g;
                if (!FloorBelow(x, y, from, true, out f, out g) || f < minFloor)
                    break;
                if (double.IsNaN(highest))
                    highest = f;
                double ceiling;
                if (CeilingAbove(x, y, f + 0.05, out ceiling) && ceiling - f >= 2.0)
                {
                    floorZ = f;
                    covered = true;
                    z = f + Math.Min(eye, Math.Max(0.5, ceiling - f - 0.5));
                    reason = string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                           "on the highest covered floor under the point ({0:0.##}; {1:0.##} yards up to the surface above)", f, ceiling - f);
                    return true;
                }
                from = f - 0.01;
            }
            if (!double.IsNaN(highest))
            {
                floorZ = highest;
                z = highest + eye;
                reason = string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                       "no covered floor under the point: on its highest floor ({0:0.##}), which has open sky above", highest);
                return true;
            }
            reason = double.IsNegativeInfinity(minFloor) ? "no floor of the world model under the point"
                                                         : string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                                                         "no floor of the world model under the point at or above {0:0.##}", minFloor);
            return false;
        }

        /// <summary>
        /// The ground at (x, y) where this world model stands on a terrain tile (world frame): the LOWEST of its floors under
        /// the point at or above `minFloor` (the terrain's ground there, less a tolerance) -- the street, or a building's ground
        /// floor -- never a floor under the terrain (a cellar) and never one high up in it (a tower's room, a roof) only
        /// because it is covered. That floor may be the lowest layer of a stack (steps, a dais, a floor laid twice): the point
        /// stands on the stack's top, climbing while the next up-facing surface is at most `eye` + 0.5 above, so the eye is
        /// never under a floor in view. z is the floor plus `eye` (less under a low ceiling); `covered` whether a surface is
        /// above it. False with the reason when no floor of the model is there.
        /// </summary>
        public bool GroundFloor(double x, double y, double eye, double minFloor, out double z, out double floorZ, out bool covered, out string reason)
        {
            z = floorZ = double.NaN;
            covered = false;
            double top = Placement.ExtentsMax.Z;
            WorldPoint bmin, bmax;
            if (Root != null)
            {
                WorldModelPlan.RootBox(Root, Placement, out bmin, out bmax);
                top = Math.Max(top, bmax.Z);
            }
            foreach (WmoGroup g in Groups)
                if (g != null)
                {
                    Placement.PlaceBox(g.Header.BoundsMin, g.Header.BoundsMax, out bmin, out bmax);
                    top = Math.Max(top, bmax.Z);
                }
            double from = top + 1.0, lowest = double.NaN;
            for (int k = 0; k < 256; k++)
            {
                double f;
                int g;
                if (!FloorBelow(x, y, from, true, out f, out g) || f < minFloor)
                    break;
                lowest = f;
                from = f - 0.01;
            }
            if (double.IsNaN(lowest))
            {
                reason = string.Format(System.Globalization.CultureInfo.InvariantCulture, "no floor of the world model under the point{0}",
                                       double.IsNegativeInfinity(minFloor) ? "" : string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                                                                                " at or above {0:0.##}", minFloor));
                return false;
            }
            double stand = lowest;
            int layers = 1;
            for (int k = 0; k < 16; k++)
            {
                double next;
                if (!FloorAbove(x, y, stand + 0.05, out next) || next - stand > eye + 0.5)
                    break;
                stand = next;
                layers++;
            }
            double ceiling;
            covered = CeilingAbove(x, y, stand + 0.05, out ceiling);
            floorZ = stand;
            z = stand + (covered ? Math.Min(eye, Math.Max(0.5, ceiling - stand - 0.5)) : eye);
            string stack = layers > 1 ? string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                                      ", the top of {0} floor layers from {1:0.##}", layers, lowest)
                                      : "";
            reason = covered ? string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                             "on its lowest floor under the point at or above the ground ({0:0.##}{1}; {2:0.##} yards up to the surface above)",
                                             stand, stack, ceiling - stand)
                             : string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                             "on its lowest floor under the point at or above the ground ({0:0.##}{1}; open sky above)", stand, stack);
            return true;
        }

        /// <summary>
        /// Where a camera may start inside this world model, from its own geometry: among its interior groups (MOGP flag
        /// 0x2000; every group when none is), the one with the largest floor -- the summed area of its floor triangles
        /// (FloorTriangles) -- and in it the floor triangle whose centroid has the most headroom up to the next surface
        /// above (at least 2.5 yards, at most 30 counted), largest triangle first among equals. A floor under a ceiling is
        /// preferred: one with open sky above is taken only when no group has a covered floor. The start is that centroid
        /// plus `eye` (less when the headroom is low), facing along the group's longer horizontal side. False, with the
        /// reason, when no group has a floor with headroom.
        /// </summary>
        public bool StartPoint(double eye, out WorldPoint start, out double yaw, out int group, out string reason)
        {
            if (StartPoint(eye, true, out start, out yaw, out group, out reason))
                return true;
            string covered = reason;
            if (StartPoint(eye, false, out start, out yaw, out group, out reason))
            {
                reason += " (no covered floor: " + covered + ")";
                return true;
            }
            return false;
        }

        bool StartPoint(double eye, bool needCeiling, out WorldPoint start, out double yaw, out int group, out string reason)
        {
            start = new WorldPoint();
            yaw = 0.0;
            group = -1;
            reason = "";
            bool anyInterior = false;
            for (int gi = 0; gi < Groups.Length; gi++)
                if (Groups[gi] != null && (Groups[gi].Header.Flags & 0x2000) != 0 && Groups[gi].Positions.Length > 0)
                    anyInterior = true;
            var floorArea = new double[Groups.Length];
            for (int gi = 0; gi < Groups.Length; gi++)
            {
                WmoGroup g = Groups[gi];
                if (g == null || g.Positions.Length == 0 || (anyInterior && (g.Header.Flags & 0x2000) == 0))
                    continue;
                foreach (FloorTriangle f in FloorTriangles(g))
                    floorArea[gi] += f.Area;
            }
            var order = new List<int>();
            for (int gi = 0; gi < Groups.Length; gi++)
                if (floorArea[gi] > 0.0)
                    order.Add(gi);
            order.Sort((a, b) => floorArea[b].CompareTo(floorArea[a]));
            foreach (int gi in order)
            {
                WmoGroup g = Groups[gi];
                var floors = FloorTriangles(g);
                floors.Sort((a, b) => b.Area.CompareTo(a.Area));
                FloorTriangle chosen = null;
                double chosenRoom = 0.0;
                int tried = 0;
                foreach (FloorTriangle f in floors)
                {
                    if (tried++ >= 64)
                        break;
                    // Headroom: from just above the floor to the next surface above (or open sky, counted as 30 yards, when
                    // a ceiling is not needed).
                    double ceiling;
                    bool covered = CeilingAbove(f.Centroid.X, f.Centroid.Y, f.Centroid.Z + 0.05, out ceiling);
                    if (!covered && needCeiling)
                        continue;
                    double room = covered ? ceiling - f.Centroid.Z : 30.0;
                    room = Math.Min(room, 30.0);
                    if (room < 2.5)
                        continue;
                    if (chosen == null || room > chosenRoom + 1e-6)
                    {
                        chosen = f;
                        chosenRoom = room;
                    }
                }
                if (chosen == null)
                    continue;
                double z = chosen.Centroid.Z + Math.Min(eye, chosenRoom - 0.5);
                start = new WorldPoint(chosen.Centroid.X, chosen.Centroid.Y, z);
                WorldPoint gmin, gmax;
                Placement.PlaceBox(g.Header.BoundsMin, g.Header.BoundsMax, out gmin, out gmax);
                // Yaw: degrees clockwise from north (+x), so +x is 0 and -y (east) is 90. Along the longer side, towards the
                // group box's far half from the start.
                double cx = 0.5 * (gmin.X + gmax.X), cy = 0.5 * (gmin.Y + gmax.Y);
                if (gmax.X - gmin.X >= gmax.Y - gmin.Y)
                    yaw = cx >= start.X ? 0.0 : 180.0;
                else
                    yaw = cy >= start.Y ? 270.0 : 90.0;
                group = gi;
                reason = string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                       "group {0} ({1}, {2:0.#} square yards of floor): the floor triangle with the most headroom " +
                                       "({3:0.##} yards up to {4}), plus the eye height", gi,
                                       (g.Header.Flags & 0x2000) != 0 ? "interior" : "exterior", floorArea[gi], chosenRoom,
                                       needCeiling ? "the ceiling above" : "open sky");
                return true;
            }
            reason = order.Count == 0 ? "no group has a floor triangle"
                                      : needCeiling ? "no floor with a ceiling at least 2.5 yards above" : "no floor with at least 2.5 yards of headroom";
            return false;
        }

        sealed class FloorTriangle
        {
            public WorldPoint Centroid;
            public double Area;
        }

        /// <summary>
        /// The group's floor triangles: those whose normal, by the file's own winding in its right-handed model space
        /// ((b - a) x (c - a)), points up in the world with z at least 0.7 of its length. The winding is the file's: in the
        /// real world models of maps 34 and 429 the up-facing horizontal triangles lie mostly in the lower half of their
        /// group box (15,323 and 24,437 against 4,358 and 3,590) and the down-facing ones in the upper half (19,064 and
        /// 10,109 against 4,893 and 4,377) -- floors face up, ceilings down (document section 7.2).
        /// </summary>
        List<FloorTriangle> FloorTriangles(WmoGroup g)
        {
            var list = new List<FloorTriangle>();
            WorldModelPlacement p = Placement;
            for (int i = 0; i + 2 < g.Indices.Length; i += 3)
            {
                int a = g.Indices[i], b = g.Indices[i + 1], c = g.Indices[i + 2];
                if (a >= g.Positions.Length || b >= g.Positions.Length || c >= g.Positions.Length)
                    continue;
                WowVec3 va = g.Positions[a], vb = g.Positions[b], vc = g.Positions[c];
                WorldPoint wa = p.ToWorld(va.X, va.Y, va.Z), wb = p.ToWorld(vb.X, vb.Y, vb.Z), wc = p.ToWorld(vc.X, vc.Y, vc.Z);
                double ux = wb.X - wa.X, uy = wb.Y - wa.Y, uz = wb.Z - wa.Z;
                double vx = wc.X - wa.X, vy = wc.Y - wa.Y, vz = wc.Z - wa.Z;
                double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                double len = Math.Sqrt(nx * nx + ny * ny + nz * nz);
                if (len <= 1e-9)
                    continue;
                // Up-facing by the file's winding (a proper rotation keeps it): a floor, not a ceiling seen from below.
                if (nz / len < 0.7)
                    continue;
                list.Add(new FloorTriangle
                {
                    Centroid = new WorldPoint((wa.X + wb.X + wc.X) / 3.0, (wa.Y + wb.Y + wc.Y) / 3.0, (wa.Z + wb.Z + wc.Z) / 3.0),
                    Area = 0.5 * len,
                });
            }
            return list;
        }

        static bool RayMeetsBox(WorldPoint o, WorldPoint d, WowVec3 min, WowVec3 max)
        {
            double t0 = 0.0, t1 = double.MaxValue;
            if (!Slab(o.X, d.X, min.X, max.X, ref t0, ref t1)) return false;
            if (!Slab(o.Y, d.Y, min.Y, max.Y, ref t0, ref t1)) return false;
            if (!Slab(o.Z, d.Z, min.Z, max.Z, ref t0, ref t1)) return false;
            return true;
        }

        static bool Slab(double o, double d, double lo, double hi, ref double t0, ref double t1)
        {
            const double pad = 1e-4;
            lo -= pad;
            hi += pad;
            if (Math.Abs(d) < 1e-12)
                return o >= lo && o <= hi;
            double a = (lo - o) / d, b = (hi - o) / d;
            if (a > b) { double s = a; a = b; b = s; }
            t0 = Math.Max(t0, a);
            t1 = Math.Min(t1, b);
            return t0 <= t1;
        }

        /// <summary>The nearest triangle of g the ray o + t d meets with 0 &lt; t &lt; limit (Moller-Trumbore). Both sides, or with
        /// frontOnly only a triangle met from its front: det = e1 . (d x e2) = -d . (e1 x e2) is positive exactly when the
        /// ray meets the side its file-winding normal points to.</summary>
        // frontOnly: a triangle met from its front only; backOnly: from its back only (a ray cast up meets a floor's back).
        static bool NearestHit(WmoGroup g, WorldPoint o, WorldPoint d, double limit, bool frontOnly, out double tBest, bool backOnly = false)
        {
            tBest = limit;
            bool hit = false;
            WowVec3[] v = g.Positions;
            for (int i = 0; i + 2 < g.Indices.Length; i += 3)
            {
                int ia = g.Indices[i], ib = g.Indices[i + 1], ic = g.Indices[i + 2];
                if (ia >= v.Length || ib >= v.Length || ic >= v.Length)
                    continue;
                double ax = v[ia].X, ay = v[ia].Y, az = v[ia].Z;
                double e1x = v[ib].X - ax, e1y = v[ib].Y - ay, e1z = v[ib].Z - az;
                double e2x = v[ic].X - ax, e2y = v[ic].Y - ay, e2z = v[ic].Z - az;
                double px = d.Y * e2z - d.Z * e2y, py = d.Z * e2x - d.X * e2z, pz = d.X * e2y - d.Y * e2x;
                double det = e1x * px + e1y * py + e1z * pz;
                if (Math.Abs(det) < 1e-12 || (frontOnly && det < 0.0) || (backOnly && det > 0.0))
                    continue;
                double inv = 1.0 / det;
                double sx = o.X - ax, sy = o.Y - ay, sz = o.Z - az;
                double u = (sx * px + sy * py + sz * pz) * inv;
                if (u < 0.0 || u > 1.0)
                    continue;
                double qx = sy * e1z - sz * e1y, qy = sz * e1x - sx * e1z, qz = sx * e1y - sy * e1x;
                double w = (d.X * qx + d.Y * qy + d.Z * qz) * inv;
                if (w < 0.0 || u + w > 1.0)
                    continue;
                double t = (e2x * qx + e2y * qy + e2z * qz) * inv;
                if (t > 1e-6 && t < tBest)
                {
                    tBest = t;
                    hit = true;
                }
            }
            return hit;
        }
    }
}
