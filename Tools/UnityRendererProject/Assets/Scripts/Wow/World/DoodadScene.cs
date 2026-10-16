// DoodadScene.cs
//
// DOODADS (stage 07, docs/world-viewer/07-doodads.md): the pure part of placing M2 doodads in the world scene -- which
// doodads a load has and where each one stands. Two sources:
//   - a tile's obj0 MDDF records (outdoor doodads), de-duplicated by uniqueId (a record repeats in every tile it
//     overlaps), placed by the Tile frame of WorldPlacements.cs -- the same rule as MODF: W = (32T - p2, 32T - p0, p1),
//     R = Rz(a1 + 180) Ry(a0) Rx(a2), scale u16/1024 (the audit proved it on the obj1 MLDX boxes, 00-audit 6.4; the
//     player's code replays that proof on four clients, Tests/DoodadCorpusTests.cs);
//   - the doodad sets of each placed world model: set 0 plus the set its placement names (MODF word 58) when that is
//     another set; each MODD record of those ranges names its M2 through MODI and is composed with the world model's
//     placement in double: W_d = W + s R p, R_d = R R(q), s_d = s s_q (p, q, s_q from MODD; q stored x, y, z, w and
//     applied as R(q), measured against the world models' walls on four clients, 07-doodads section 3).
// Pure C#, no UnityEngine: the parser tests run it outside the editor (Tests/WorldModelTests.cs,
// Tests/DoodadCorpusTests.cs).

using System;
using System.Collections.Generic;

namespace Wmv.Wow.World
{
    public enum DoodadSource
    {
        /// <summary>An obj0 MDDF record.</summary>
        Mddf,
        /// <summary>A MODD record of a placed world model's active doodad set.</summary>
        WmoSet,
    }

    /// <summary>One doodad of a scene and where it stands (game frame).</summary>
    public sealed class WorldDoodad
    {
        public DoodadSource Source;
        /// <summary>The M2's FileDataID; 0 when the record names none.</summary>
        public uint FileDataID;
        /// <summary>MDDF: the record's uniqueId. WMO doodad: its world model placement's uniqueId.</summary>
        public uint UniqueId;
        /// <summary>MDDF: records of the source with this uniqueId (repeated in several obj0 files: one doodad).</summary>
        public int Records = 1;
        /// <summary>WMO doodad: its MODD record, its set, its world model's root FileDataID; -1/-1/0 for MDDF.</summary>
        public int ModdIndex = -1;
        public int Set = -1;
        public uint WmoFileDataID;
        /// <summary>The world model placement (index in the scene's placement list) a WMO doodad belongs to; -1 for MDDF.</summary>
        public int Owner = -1;
        /// <summary>W: where the model's origin stands.</summary>
        public WorldPoint Position;
        /// <summary>R: row-major 3x3, world axes.</summary>
        public double[] Rotation;
        public double Scale = 1.0;
        /// <summary>Why this doodad cannot be placed (no FileDataID, a name table entry, an index past MODI), or null.</summary>
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

        /// <summary>A model-space box placed in the world (all eight corners).</summary>
        public void PlaceBox(WorldPoint min, WorldPoint max, out WorldPoint wmin, out WorldPoint wmax)
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
                                 "{0} M2 {1} uniqueId {2}{3} at {4} scale {5:0.###}{6}", Source == DoodadSource.Mddf ? "MDDF" : "WMO doodad",
                                 FileDataID, UniqueId,
                                 Source == DoodadSource.WmoSet ? string.Format(" (WMO {0} MODD {1} set {2})", WmoFileDataID, ModdIndex, Set) : "",
                                 Position, Scale, Unsupported != null ? " -- unsupported: " + Unsupported : "");
        }
    }

    /// <summary>The doodad sets a world model placement shows.</summary>
    public sealed class WmoDoodadSelection
    {
        public uint UniqueId;
        public uint WmoFileDataID;
        /// <summary>The set the placement names (MODF word 58).</summary>
        public int Requested;
        /// <summary>The root's MODS count.</summary>
        public int SetCount;
        /// <summary>The sets drawn: 0, plus Requested when it is another valid set.</summary>
        public List<int> Active = new List<int>();
        /// <summary>The active sets' names, in order.</summary>
        public List<string> Names = new List<string>();
        /// <summary>MODD records in the active sets (after clamping each range to the MODD count).</summary>
        public int Records;
        /// <summary>A remark on the selection (an out-of-range set, a range past MODD), or null.</summary>
        public string Reason;
    }

    public static class DoodadPlan
    {
        /// <summary>
        /// The outdoor doodads of a tile from its MDDF records: one per uniqueId, the first record kept and the others
        /// counted (a repeated record with any difference is noted -- the audit never saw one); each placed by the frame's
        /// transform (the Tile frame for an obj0). A record that does not name its model by FileDataID (flag 0x40 clear: a
        /// name-table entry, which no audited client's obj0 has) or names FileDataID 0 is kept with Unsupported set.
        /// </summary>
        public static List<WorldDoodad> FromMddf(IList<DoodadPlacement> records, PlacementFrame frame, List<string> notes)
        {
            var list = new List<WorldDoodad>();
            if (records == null)
                return list;
            var byId = new Dictionary<uint, int>();
            var first = new Dictionary<uint, DoodadPlacement>();
            foreach (DoodadPlacement r in records)
            {
                int at;
                if (byId.TryGetValue(r.UniqueId, out at))
                {
                    list[at].Records++;
                    if (notes != null && !first[r.UniqueId].SamePlacementAs(r))
                        notes.Add(string.Format("MDDF uniqueId {0} is repeated with different contents; the first record is used", r.UniqueId));
                    continue;
                }
                var d = new WorldDoodad
                {
                    Source = DoodadSource.Mddf,
                    UniqueId = r.UniqueId,
                    FileDataID = r.NameKind == PlacementNameKind.FileDataId ? r.NameId : 0,
                    Position = PlacementTransform.ToWorld(r.Position, frame),
                    Rotation = PlacementTransform.Rotation(r.Rotation),
                    Scale = r.Scale,
                };
                if (r.NameKind != PlacementNameKind.FileDataId)
                    d.Unsupported = "the record names its model by a name-table entry (MDDF flag 0x40 clear), not by a FileDataID";
                else if (r.NameId == 0)
                    d.Unsupported = "the record names FileDataID 0";
                else if (r.NameId > int.MaxValue)
                    d.Unsupported = "FileDataID " + r.NameId + " is beyond what a request can carry";
                else if (r.ScaleRaw == 0)
                    d.Unsupported = "the record's scale is 0";
                byId[r.UniqueId] = list.Count;
                first[r.UniqueId] = r;
                list.Add(d);
            }
            return list;
        }

        /// <summary>
        /// The doodad sets a placement shows: set 0 (every root's default set) plus the set the placement names, when that
        /// is another set of the root. A requested set past the root's sets is noted and only set 0 is drawn. The rule is
        /// the one every source in the repository uses (the audit's counter, the archived host code); which sets the game
        /// draws is not stored in any file (07-doodads section 3.4).
        /// </summary>
        public static WmoDoodadSelection SelectSets(WmoRoot root, uint uniqueId, uint wmo, int requested)
        {
            var sel = new WmoDoodadSelection { UniqueId = uniqueId, WmoFileDataID = wmo, Requested = requested };
            WmoDoodadSet[] sets = root != null ? root.DoodadSets : new WmoDoodadSet[0];
            sel.SetCount = sets.Length;
            if (sets.Length == 0)
            {
                sel.Reason = "the world model has no doodad sets";
                return sel;
            }
            sel.Active.Add(0);
            if (requested > 0 && requested < sets.Length)
                sel.Active.Add(requested);
            else if (requested >= sets.Length)
                sel.Reason = string.Format("its placement names doodad set {0}, but the world model has {1}: set 0 only", requested, sets.Length);
            int modd = root.DoodadDefs.Length;
            foreach (int s in sel.Active)
            {
                sel.Names.Add(sets[s].Name ?? "");
                long start = sets[s].StartIndex, end = start + sets[s].Count;
                if (end > modd)
                {
                    sel.Reason = (sel.Reason != null ? sel.Reason + "; " : "") +
                                 string.Format("set {0} runs past the {1} MODD records (to {2})", s, modd, end);
                    end = modd;
                }
                if (end > start)
                    sel.Records += (int)(end - start);
            }
            return sel;
        }

        /// <summary>
        /// A DIAGNOSTIC selection (-wmvDoodadSets=all): every set of the root, the inactive ones too -- what the default rule
        /// leaves out, drawn for comparison. Never the scene's own rule.
        /// </summary>
        public static WmoDoodadSelection AllSets(WmoRoot root, uint uniqueId, uint wmo, int requested)
        {
            WmoDoodadSelection sel = SelectSets(root, uniqueId, wmo, requested);
            WmoDoodadSet[] sets = root != null ? root.DoodadSets : new WmoDoodadSet[0];
            sel.Active.Clear();
            sel.Names.Clear();
            sel.Records = 0;
            int modd = root != null ? root.DoodadDefs.Length : 0;
            for (int s = 0; s < sets.Length; s++)
            {
                sel.Active.Add(s);
                sel.Names.Add(sets[s].Name ?? "");
                long start = sets[s].StartIndex, end = Math.Min(start + sets[s].Count, modd);
                if (end > start)
                    sel.Records += (int)(end - start);
            }
            if (sets.Length > 0)
                sel.Reason = "DIAGNOSTIC: every doodad set drawn (-wmvDoodadSets=all)" + (sel.Reason != null ? "; " + sel.Reason : "");
            return sel;
        }

        /// <summary>
        /// The doodads of a placed world model: every MODD record of its active sets, its M2 named through MODI, placed in
        /// the world by composing the record with the placement in double (W_d = W + s R p, R_d = R R(q), s_d = s s_q).
        /// A record whose name index is past MODI, or names FileDataID 0, is kept with Unsupported set.
        /// </summary>
        public static List<WorldDoodad> FromWmo(WorldModelPlacement wmo, int owner, WmoRoot root, WmoDoodadSelection sel)
        {
            var list = new List<WorldDoodad>();
            if (wmo == null || root == null || sel == null)
                return list;
            WmoDoodadSet[] sets = root.DoodadSets;
            WmoDoodadDef[] defs = root.DoodadDefs;
            uint[] ids = root.DoodadFileDataIDs;
            foreach (int s in sel.Active)
            {
                long start = sets[s].StartIndex, end = Math.Min(start + sets[s].Count, defs.Length);
                for (long k = start; k < end; k++)
                {
                    WmoDoodadDef def = defs[k];
                    var d = new WorldDoodad
                    {
                        Source = DoodadSource.WmoSet,
                        UniqueId = wmo.UniqueId,
                        ModdIndex = (int)k,
                        Set = s,
                        WmoFileDataID = wmo.FileDataID,
                        Owner = owner,
                        Position = wmo.ToWorld(def.Position.X, def.Position.Y, def.Position.Z),
                        Rotation = PlacementTransform.Mul(wmo.Rotation, QuaternionMatrix(def.Rotation)),
                        Scale = wmo.Scale * def.Scale,
                    };
                    if (def.NameIndex >= ids.Length)
                        d.Unsupported = string.Format("MODD {0} names MODI entry {1}, past the {2} entries", k, def.NameIndex, ids.Length);
                    else if (ids[def.NameIndex] == 0)
                        d.Unsupported = string.Format("MODD {0} names MODI entry {1}, which is 0", k, def.NameIndex);
                    else if (ids[def.NameIndex] > int.MaxValue)
                        d.Unsupported = "FileDataID " + ids[def.NameIndex] + " is beyond what a request can carry";
                    else
                        d.FileDataID = ids[def.NameIndex];
                    if (!(def.Scale > 0f) || float.IsInfinity(def.Scale))
                        d.Unsupported = d.Unsupported ?? string.Format("MODD {0} has scale {1}", k, def.Scale);
                    list.Add(d);
                }
            }
            return list;
        }

        /// <summary>
        /// The rotation of a MODD quaternion (stored x, y, z, w), row-major, as R(q) (not its transpose): the convention
        /// that keeps wall-standing doodads out of the walls (07-doodads section 3.3). Normalised first (every stored
        /// quaternion of the four clients has norm 1 to 1e-3; a zero quaternion is the identity).
        /// </summary>
        public static double[] QuaternionMatrix(WowQuat q)
        {
            double x = q.X, y = q.Y, z = q.Z, w = q.W;
            double n = Math.Sqrt(x * x + y * y + z * z + w * w);
            if (!(n > 1e-9) || double.IsInfinity(n))
                return new[] { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };
            x /= n; y /= n; z /= n; w /= n;
            return new[]
            {
                1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y),
            };
        }

        /// <summary>
        /// A world model built without some of its group files: each doodad whose MODD record only groups that could not be
        /// read list (MODR) would stand where no geometry was built -- it is marked Unsupported, said. A record no group
        /// lists is kept (no group claims it). `owners` is GroupMembership's, `groups` the world model's LOD0 groups as
        /// built (null where a file could not be read). Returns how many were marked.
        /// </summary>
        public static int MarkMissingGroups(List<WorldDoodad> doodads, List<int>[] owners, WmoGroup[] groups)
        {
            int marked = 0;
            if (doodads == null || owners == null || groups == null)
                return 0;
            foreach (WorldDoodad d in doodads)
            {
                if (d.Unsupported != null || d.ModdIndex < 0 || d.ModdIndex >= owners.Length || owners[d.ModdIndex] == null)
                    continue;
                bool any = false;
                foreach (int g in owners[d.ModdIndex])
                    if (g >= 0 && g < groups.Length && groups[g] != null)
                    {
                        any = true;
                        break;
                    }
                if (any)
                    continue;
                d.Unsupported = "it stands in group(s) " + string.Join(", ", owners[d.ModdIndex]) + " of its world model, whose file(s) could not be read";
                marked++;
            }
            return marked;
        }

        /// <summary>
        /// MODR, decoded: per MODD record, the LOD0 groups that list it (u16 indices into MODD). A record listed by
        /// several groups appears once in the scene; groups whose MODR is absent or malformed list nothing.
        /// </summary>
        public static List<int>[] GroupMembership(WmoRoot root, WmoGroup[] groups)
        {
            int n = root != null ? root.DoodadDefs.Length : 0;
            var owners = new List<int>[n];
            if (groups == null)
                return owners;
            for (int g = 0; g < groups.Length; g++)
            {
                WmoGroup grp = groups[g];
                WmoChunkInfo chunk;
                if (grp == null || !grp.TryGetChunk("MODR", out chunk))
                    continue;
                byte[] b = grp.CopyChunkBytes(chunk);
                for (int i = 0; i + 1 < b.Length; i += 2)
                {
                    int k = b[i] | (b[i + 1] << 8);
                    if (k >= n)
                        continue;
                    if (owners[k] == null)
                        owners[k] = new List<int>();
                    if (!owners[k].Contains(g))
                        owners[k].Add(g);
                }
            }
            return owners;
        }
    }
}
