// ModelLight.cs
//
// THE OUTDOOR LIGHT ON WORLD MODELS AND DOODADS (Stage 10 step 6, docs/world-viewer/10-light-sky-fog.md 2.2, 6.6, 6.7, 6.10,
// 6.13, 6.15): the client's M2 and world-model light, in pure C# (no UnityEngine), and the context rule that decides which
// world renderers take it. Resources/WmvWorldLight.hlsl (WmvWorldModelShade) evaluates the light in the world-lit variants
// WmvOpaqueWorld.shader and WmvWmoWorld.shader; the lifecycle self-test compares the GPU with Shade;
// Tests/WorldModelLightTests.cs checks Shade against the client's own programs executed at the Southfury light, and the
// contexts on synthetic world models (and, with the corpus, on the client's).
//
//   the light    the client's M2 hemisphere programs (combiners_opaque 2977063 p_1 and the other M2 containers: 2,688 lit
//                programs) and the world-model hemisphere programs (material3_wmo 5446718 p_1, uber 2977272 p_1) share one
//                core, identical in lighting mode 1 (PROVEN_BYTES, executed):
//                  nn     = normalize(n), n the interpolated normal as it comes (unit at the vertices: the vertex programs
//                           normalise it, M2 2977756 v_1:61-72, world model 5446713 p_1:73-78; Unity's
//                           UnityObjectToWorldNormal does the same for the uniform scales every placement has)
//                  H      = nn.U >= 0 ? lerp(Horizon, Ambient, nn.U) : lerp(Horizon, Ground, -nn.U), U world up
//                           (HorizonAmbientColor or GroundAmbientColor 0 -> AmbientColor: INFERRED (public), the CPU's rule)
//                  A      = H x (0.7 + 0.4 x (0.5 + 0.5 x nn.L)) x AO      nn.L NOT saturated (M2 p_1:219-222, WMO p_1:185-188)
//                  D      = Direct x sat(n.L) x attenuation x shadow       dp3_sat on the RAW interpolant (M2 p_1:223, WMO
//                           p_1:189); the shadow on D only
//                  colour = sqrt((base x (A + D))^2 + base^2 x local) + the second directional term
//                           (M2 p_1:245-246 and :278-279, WMO p_1:214-215 and :247-248); no saturate anywhere on the lit colour
//                The differences from the terrain's program (TerrainLight.cs), all by the bytes: H and the wrap take the
//                normalised normal, the direct term the raw one (the terrain uses its raw MCNR vector everywhere); the base is
//                not MCCV x 2; there is no specular term (0 of 2,688 M2 programs read cb8[7]); and the FLAT class applies the
//                shadow to D only (M2 2977063 p_203:199-212, world model 5446718 p_17:164-176) where the terrain's flat class
//                multiplies the whole colour by 0.7 + 0.3 x shadow.
//   the base     doodads (M2): the combiner x _Color, as the player computes c.rgb today. The diffuse_* vertex families write
//                COLOR0 = 0.5 x sat(cb0[7] + cb0[8]) or 0.5 x cb0[7] (2977756 v_1:57-58, v_5:57) and the pixel program
//                doubles it (2977063 p_1:180-181), so no factor is left (PROVEN_BYTES for those families; which family a
//                batch binds is INFERRED (public); cb0[7] the colour track, cb0[8] 0: INFERRED). Exterior world-model
//                groups: albedo x COLOR0 x 2 with the vertex programs' gate-off COLOR0 (0.5, 0.49999, 0.5) (5446713 p_1:89,
//                2977862 p_1:89; the x 2 at 5446718 p_1:146-147): albedo x (1, 0.99998, 1). The constants PROVEN_BYTES;
//                the gate being off for exterior batches INFERRED (the data argues it only for groups that carry MOCV).
//   the holds    Stage 10 feeds each of the client's inputs at its neutral value: local light 0, AO 1, attenuation 1, shadow
//                1 (cast shadows are not drawn, D10, plan 6.15: right on back-facing pixels, wrong on light-facing ones at
//                dawn and dusk), the second directional term 0, the override weight 0, the exterior MOCV light E 0 (deferred:
//                its k and per-batch mode are UNKNOWN), the M2 glow and desaturation 0, the world model's per-material colour
//                lerp 0, the flat class's K (cb8[9]) 0. With them the colour is exactly base x (A + D). Each is an input of
//                its own, applied where the client applies it, so a later stage feeds it without a change here.
//   the flat     (-wmvDoodadVariant=flat, -wmvWorldModelVariant=flat; for the record: measured against, not drawn by
//   class        default) base x sat(local + Ambient x AO + D), K = 0, the shadow on D only (M2 2977063 p_195).
//   unlit        lighting mode 0 returns the base (PROVEN_BYTES, executed): the M2 material flag 0x01 and additive batches
//                (today's choice for those without 0x01: their mode is UNKNOWN), world-model F_UNLIT (the green 0.99998 of
//                mode 0 is not applied: today's bypass). That the flags select mode 0 is INFERRED (public).
//   the contexts (WorldLightContexts, plan 6.7) which world renderers take the light. An exterior group (MOGP 0x8 without
//                0x2000) is OUTDOOR; an interior group (0x2000) and a group with neither or both flags (never seen) keep
//                today's look until Stage 12 (INTERIOR_INTERIM). An MDDF doodad is OUTDOOR. A MODD record listed (MODR)
//                only by exterior groups is OUTDOOR; one listed by an interior group, by both kinds, by no read group, or
//                of a world model built without some of its group files (its MODR is incomplete) is INTERIOR_INTERIM. The
//                groups' flags are the group files' (MOGP), never the root's MOGI copy. MDDF doodads that stand inside an
//                interior group's box are counted (MddfInsideInterior): they take the outdoor light beside rig-lit walls.
// Directions in Unity's frame (up +y; WorldToUnity.Direction), L towards the light and unit, colours authored (byte / 255).
// Thread safety: no mutable static state.

using System;
using System.Collections.Generic;
using System.Globalization;

namespace Wmv.Wow.World
{
    /// <summary>Which light a world renderer takes (see the file header).</summary>
    public enum WorldLightContext
    {
        /// <summary>Today's look (the builder's material on the original shader) until Stage 12: interior groups, the
        /// doodads they list, and every case the rule does not place. The default.</summary>
        InteriorInterim = 0,
        /// <summary>The outdoor light: exterior world-model groups and outdoor doodads.</summary>
        Outdoor = 1,
    }

    /// <summary>The client's M2 and world-model light (see the file header), the CPU twin of WmvWorldLight.hlsl's
    /// WmvWorldModelShade.</summary>
    public static class ModelOutdoorLight
    {
        /// <summary>The wrap A = H (WrapBase + WrapScale (WrapHalf + WrapHalf nn.L)) AO (PROVEN_BYTES: M2 2977063 p_1:219-222,
        /// world model 5446718 p_1:185-188).</summary>
        public const double WrapBase = 0.7, WrapScale = 0.4, WrapHalf = 0.5;
        public const string WrapGrade = "PROVEN_BYTES";
        /// <summary>The world model's base factor: the pixel program's 2 x the vertex programs' gate-off COLOR0 (0.5, 0.49999,
        /// 0.5) (PROVEN_BYTES; the gate off for exterior batches INFERRED).</summary>
        public const double WmoGateOffRed = 2.0 * 0.5, WmoGateOffGreen = 2.0 * 0.49999, WmoGateOffBlue = 2.0 * 0.5;
        /// <summary>The M2 base factor left over: COLOR0 = 0.5 x the colour, doubled by the pixel program (PROVEN_BYTES for the
        /// diffuse_* vertex families).</summary>
        public const double M2BaseFactor = 1.0;
        /// <summary>The client's inputs Stage 10 holds at their neutral values (see the file header).</summary>
        public const double Attenuation = 1.0, Shadow = 1.0, Ao = 1.0, Local = 0.0, SecondLight = 0.0, OverrideWeight = 0.0,
                            MocvLight = 0.0, FlatK = 0.0, Glow = 0.0, Desaturation = 0.0;

        /// <summary>The world model's base factor (r, g, b): (1, 0.99998, 1). A new array each call.</summary>
        public static double[] WmoGateOffBase { get { return new[] { WmoGateOffRed, WmoGateOffGreen, WmoGateOffBlue }; } }

        static double Sat(double x) { return x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : x; }

        static bool Zero(double[] c) { return c[0] == 0.0 && c[1] == 0.0 && c[2] == 0.0; }

        /// <summary>
        /// The light factor F (r, g, b) at a pixel: the hemisphere class's A + D (A with `ao`, D with `shadow`), or the flat
        /// class's sat(Ambient x ao + D) (K = 0, no local light). nRaw is the interpolated normal as it comes (Unity, not
        /// normalised here: H and the wrap normalise it, the direct term uses it raw), l the unit vector towards the light,
        /// the colours authored (r, g, b).
        /// </summary>
        public static void Factor(double[] nRaw, double[] l, double[] ambient, double[] horizon, double[] ground, double[] direct, bool flat,
                                  double shadow, double ao, double[] f)
        {
            double lit = Sat(nRaw[0] * l[0] + nRaw[1] * l[1] + nRaw[2] * l[2]);   // dp3_sat on the RAW interpolant
            if (flat)
            {
                for (int c = 0; c < 3; c++)
                    f[c] = Sat(ambient[c] * ao + direct[c] * lit * Attenuation * shadow);
                return;
            }
            double len = Math.Sqrt(nRaw[0] * nRaw[0] + nRaw[1] * nRaw[1] + nRaw[2] * nRaw[2]);
            double nx = nRaw[0] / len, ny = nRaw[1] / len, nz = nRaw[2] / len;      // H and the wrap: normalised
            double nl = nx * l[0] + ny * l[1] + nz * l[2];                         // NOT saturated
            double nu = ny;                                                        // U = world up (Unity +y)
            double[] hz = Zero(horizon) ? ambient : horizon;
            double[] gr = Zero(ground) ? ambient : ground;
            double wrap = WrapBase + WrapScale * (WrapHalf + WrapHalf * nl);
            for (int c = 0; c < 3; c++)
            {
                double h = nu >= 0.0 ? hz[c] + (ambient[c] - hz[c]) * nu : hz[c] + (gr[c] - hz[c]) * (-nu);
                f[c] = h * wrap * ao + direct[c] * lit * Attenuation * shadow;
            }
        }

        /// <summary>
        /// The lit colour of `base` (r, g, b, not saturated) exactly as WmvWorldLight.hlsl's WmvWorldModelShade computes it,
        /// in double: the hemisphere class sqrt((base (A + D))^2 + base^2 local) + the second directional term, or the flat
        /// class base x sat(local + Ambient x ao + D). The same arguments as the shader function, plus the globals it reads
        /// (l, the four colours). A zero normal gives NaN in the hemisphere class, as the shader's normalize does.
        /// </summary>
        public static void Shade(double[] baseRgb, double[] nRaw, double[] l, double[] ambient, double[] horizon, double[] ground, double[] direct,
                                 bool flat, double shadow, double ao, double local, double[] rgb)
        {
            double lit = Sat(nRaw[0] * l[0] + nRaw[1] * l[1] + nRaw[2] * l[2]);
            if (flat)
            {
                for (int c = 0; c < 3; c++)
                    rgb[c] = baseRgb[c] * Sat(local + ambient[c] * ao + direct[c] * lit * Attenuation * shadow);
                return;
            }
            double len = Math.Sqrt(nRaw[0] * nRaw[0] + nRaw[1] * nRaw[1] + nRaw[2] * nRaw[2]);
            double nx = nRaw[0] / len, ny = nRaw[1] / len, nz = nRaw[2] / len;
            double nl = nx * l[0] + ny * l[1] + nz * l[2];
            double nu = ny;
            double[] hz = Zero(horizon) ? ambient : horizon;
            double[] gr = Zero(ground) ? ambient : ground;
            double wrap = WrapBase + WrapScale * (WrapHalf + WrapHalf * nl);
            for (int c = 0; c < 3; c++)
            {
                double h = nu >= 0.0 ? hz[c] + (ambient[c] - hz[c]) * nu : hz[c] + (gr[c] - hz[c]) * (-nu);
                double a = h * wrap * ao;
                double d = direct[c] * lit * Attenuation * shadow;
                double x = baseRgb[c] * (a + d);
                rgb[c] = Math.Sqrt(x * x + baseRgb[c] * baseRgb[c] * local) + SecondLight;
            }
        }

        /// <summary>A doodad's lit colour: Shade of the M2 base (the combiner x _Color: no factor) with Stage 10's holds.</summary>
        public static void ShadeM2(double[] albedo, double[] nRaw, double[] l, double[] ambient, double[] horizon, double[] ground, double[] direct,
                                   bool flat, double[] rgb)
        {
            Shade(albedo, nRaw, l, ambient, horizon, ground, direct, flat, Shadow, Ao, Local, rgb);
        }

        /// <summary>An exterior world-model group's lit colour: Shade of albedo x (1, 0.99998, 1) with Stage 10's holds.</summary>
        public static void ShadeWmo(double[] albedo, double[] nRaw, double[] l, double[] ambient, double[] horizon, double[] ground, double[] direct,
                                    bool flat, double[] rgb)
        {
            var b = new[] { albedo[0] * WmoGateOffRed, albedo[1] * WmoGateOffGreen, albedo[2] * WmoGateOffBlue };
            Shade(b, nRaw, l, ambient, horizon, ground, direct, flat, Shadow, Ao, Local, rgb);
        }

        /// <summary>An unlit batch (M2 0x01 or additive, world-model F_UNLIT) under the light: lighting mode 0, the base as
        /// it is (the world model's 0.99998 green not applied: today's bypass).</summary>
        public static void Unlit(double[] albedo, double[] rgb)
        {
            rgb[0] = albedo[0];
            rgb[1] = albedo[1];
            rgb[2] = albedo[2];
        }
    }

    /// <summary>The contexts of one world model's groups and MODD records (WorldLightContexts.Modd) and their census.</summary>
    public sealed class ModdContexts
    {
        /// <summary>Listing bits: a read exterior group lists the record, a read interior group (0x2000, or neither or both
        /// flags) lists it.</summary>
        public const byte ByExterior = 1, ByInterior = 2;

        /// <summary>Per LOD0 group of the array given: its context (InteriorInterim for a group that was not read).</summary>
        public WorldLightContext[] Groups = new WorldLightContext[0];
        /// <summary>Per MODD record: its context.</summary>
        public WorldLightContext[] Records = new WorldLightContext[0];
        /// <summary>Per MODD record: the kinds of read group that list it (ByExterior | ByInterior; 0 none).</summary>
        public byte[] Listing = new byte[0];
        /// <summary>Some LOD0 group was not read: its MODR is unknown, so every record is InteriorInterim (a record a missing
        /// interior group lists could otherwise come out Outdoor).</summary>
        public bool Incomplete;

        /// <summary>The groups read: exterior, interior (with neither or both flags among them, and those with 0x2000 and
        /// 0x40); the groups not read.</summary>
        public int ExteriorGroups, InteriorGroups, NeitherOrBothGroups, InteriorExteriorLitGroups, MissingGroups;
        /// <summary>The records by context, and by listing: only by exterior groups, only by interior groups, by both kinds,
        /// by no read group.</summary>
        public int Outdoor, InteriorInterim, ListedByExteriorOnly, ListedByInteriorOnly, ListedByBoth, ListedByNone;
        /// <summary>The MODD indices listed by both kinds of group, ascending.</summary>
        public readonly List<int> BothIndices = new List<int>();

        /// <summary>Records made InteriorInterim because the world model is incomplete (all of them, or 0).</summary>
        public int IncompleteRecords { get { return Incomplete ? Records.Length : 0; } }

        /// <summary>The context of MODD record k; InteriorInterim for an index outside the records.</summary>
        public WorldLightContext Of(int moddIndex)
        {
            return moddIndex >= 0 && moddIndex < Records.Length ? Records[moddIndex] : WorldLightContext.InteriorInterim;
        }

        public override string ToString()
        {
            return string.Format(CultureInfo.InvariantCulture,
                                 "{0} group(s): {1} exterior, {2} interior ({3} with neither or both flags; {4} interior with 0x40){5}; " +
                                 "{6} MODD record(s): {7} outdoor, {8} interior-interim{9} (listed by exterior groups only {10}, interior only {11}, " +
                                 "both {12}{13}, none {14})",
                                 Groups.Length, ExteriorGroups, InteriorGroups, NeitherOrBothGroups, InteriorExteriorLitGroups,
                                 MissingGroups > 0 ? ", " + MissingGroups + " not read" : "", Records.Length, Outdoor, InteriorInterim,
                                 Incomplete ? " (incomplete: every record)" : "", ListedByExteriorOnly, ListedByInteriorOnly, ListedByBoth,
                                 BothIndices.Count > 0 ? ": MODD " + string.Join(", ", BothIndices) : "", ListedByNone);
        }
    }

    /// <summary>MDDF doodads standing inside an interior group's box (WorldLightContexts.MddfInsideInterior).</summary>
    public sealed class InteriorDoodadCensus
    {
        /// <summary>The MDDF doodads looked at (placeable ones: Unsupported null).</summary>
        public int Doodads;
        /// <summary>Of those, the ones whose origin lies inside an interior group's MOGP box of some placed world model.</summary>
        public int Inside;
        /// <summary>The first few of them, named.</summary>
        public readonly List<string> Names = new List<string>();

        public override string ToString()
        {
            return string.Format(CultureInfo.InvariantCulture, "{0} of {1} MDDF doodad(s) stand inside an interior group's box{2}", Inside, Doodads,
                                 Names.Count > 0 ? ": " + string.Join("; ", Names.ToArray()) + (Inside > Names.Count ? "; ..." : "") : "");
        }
    }

    /// <summary>The context rule (see the file header; plan 6.7).</summary>
    public static class WorldLightContexts
    {
        /// <summary>MOGP 0x40, named "exterior-lit" by public documentation: counted on interior groups, never acted on.</summary>
        public const uint ExteriorLitFlag = 0x40;
        /// <summary>The grade of the group and doodad rules (public documentation, plan 6.7); the cases they leave open are
        /// choices that keep today's look.</summary>
        public const string RuleGrade = "INFERRED (public)";

        /// <summary>A group's context from its MOGP flags: Outdoor with 0x8 and without 0x2000, else InteriorInterim;
        /// `neitherOrBoth` when the group carries neither flag or both (never seen in the client's files).</summary>
        public static WorldLightContext Group(uint mogpFlags, out bool neitherOrBoth)
        {
            bool outdoor = (mogpFlags & WmoGroupFlags.Outdoor) != 0, indoor = (mogpFlags & WmoGroupFlags.Indoor) != 0;
            neitherOrBoth = outdoor == indoor;
            return outdoor && !indoor ? WorldLightContext.Outdoor : WorldLightContext.InteriorInterim;
        }

        public static WorldLightContext Group(uint mogpFlags)
        {
            bool unused;
            return Group(mogpFlags, out unused);
        }

        /// <summary>
        /// The contexts of a world model's LOD0 groups and MODD records: `groups` the LOD0 groups as built (GFID order, null
        /// where a file could not be read), their flags the group files' own (MOGP). A record listed (MODR) only by exterior
        /// groups is Outdoor; any other record, and every record when some group was not read, InteriorInterim. Built on
        /// DoodadPlan.GroupMembership.
        /// </summary>
        public static ModdContexts Modd(WmoRoot root, WmoGroup[] groups)
        {
            var r = new ModdContexts();
            int n = root != null ? root.DoodadDefs.Length : 0;
            int g = groups != null ? groups.Length : 0;
            r.Groups = new WorldLightContext[g];
            r.Records = new WorldLightContext[n];
            r.Listing = new byte[n];
            r.Incomplete = groups == null;
            for (int i = 0; i < g; i++)
            {
                if (groups[i] == null)
                {
                    r.Groups[i] = WorldLightContext.InteriorInterim;
                    r.MissingGroups++;
                    r.Incomplete = true;
                    continue;
                }
                bool odd;
                uint flags = groups[i].Header.Flags;
                r.Groups[i] = Group(flags, out odd);
                if (r.Groups[i] == WorldLightContext.Outdoor)
                    r.ExteriorGroups++;
                else
                {
                    r.InteriorGroups++;
                    if (odd)
                        r.NeitherOrBothGroups++;
                    if ((flags & WmoGroupFlags.Indoor) != 0 && (flags & ExteriorLitFlag) != 0)
                        r.InteriorExteriorLitGroups++;
                }
            }
            List<int>[] owners = DoodadPlan.GroupMembership(root, groups);
            for (int k = 0; k < n; k++)
            {
                byte listing = 0;
                if (owners[k] != null)
                    foreach (int o in owners[k])
                        if (o >= 0 && o < g && groups[o] != null)
                            listing |= r.Groups[o] == WorldLightContext.Outdoor ? ModdContexts.ByExterior : ModdContexts.ByInterior;
                r.Listing[k] = listing;
                if (listing == ModdContexts.ByExterior)
                    r.ListedByExteriorOnly++;
                else if (listing == ModdContexts.ByInterior)
                    r.ListedByInteriorOnly++;
                else if (listing != 0)
                {
                    r.ListedByBoth++;
                    r.BothIndices.Add(k);
                }
                else
                    r.ListedByNone++;
                r.Records[k] = !r.Incomplete && listing == ModdContexts.ByExterior ? WorldLightContext.Outdoor : WorldLightContext.InteriorInterim;
                if (r.Records[k] == WorldLightContext.Outdoor)
                    r.Outdoor++;
                else
                    r.InteriorInterim++;
            }
            return r;
        }

        /// <summary>
        /// The MDDF doodads of a load whose origin lies inside an interior group's box of a placed world model: for each
        /// placeable MDDF doodad and each placement whose MODF extents (the world box) hold its origin, the origin taken into
        /// the model's frame by the inverse of the placement transform (WorldModelPlacement.ToModel: the rotation transposed,
        /// divided by the scale) and tested against every read InteriorInterim group's MOGP box (model space, inclusive).
        /// Each doodad is counted once; the first `maxNames` are named. Such doodads take the outdoor light (the rule,
        /// plan 6.7); the count says how many stand beside rig-lit interior walls until Stage 12.
        /// </summary>
        public static InteriorDoodadCensus MddfInsideInterior(IList<WorldDoodad> doodads, IList<WorldModelGeometry> models, int maxNames = 5)
        {
            var census = new InteriorDoodadCensus();
            if (doodads == null)
                return census;
            foreach (WorldDoodad d in doodads)
            {
                if (d == null || d.Source != DoodadSource.Mddf || d.Unsupported != null)
                    continue;
                census.Doodads++;
                if (models == null)
                    continue;
                WorldPoint w = d.Position;
                string hit = null;
                foreach (WorldModelGeometry m in models)
                {
                    WorldModelPlacement p = m != null ? m.Placement : null;
                    if (p == null || p.Unsupported != null || m.Groups == null)
                        continue;
                    if (w.X < p.ExtentsMin.X || w.X > p.ExtentsMax.X || w.Y < p.ExtentsMin.Y || w.Y > p.ExtentsMax.Y ||
                        w.Z < p.ExtentsMin.Z || w.Z > p.ExtentsMax.Z)
                        continue;
                    WorldPoint q = p.ToModel(w.X, w.Y, w.Z);
                    for (int gi = 0; gi < m.Groups.Length && hit == null; gi++)
                    {
                        WmoGroup grp = m.Groups[gi];
                        if (grp == null || Group(grp.Header.Flags) != WorldLightContext.InteriorInterim)
                            continue;
                        WowVec3 mn = grp.Header.BoundsMin, mx = grp.Header.BoundsMax;
                        if (q.X >= mn.X && q.X <= mx.X && q.Y >= mn.Y && q.Y <= mx.Y && q.Z >= mn.Z && q.Z <= mx.Z)
                            hit = string.Format(CultureInfo.InvariantCulture, "M2 {0} uniqueId {1} in WMO {2} uniqueId {3} group {4}", d.FileDataID,
                                                d.UniqueId, p.FileDataID, p.UniqueId, gi);
                    }
                    if (hit != null)
                        break;
                }
                if (hit == null)
                    continue;
                census.Inside++;
                if (census.Names.Count < maxNames)
                    census.Names.Add(hit);
            }
            return census;
        }
    }
}
