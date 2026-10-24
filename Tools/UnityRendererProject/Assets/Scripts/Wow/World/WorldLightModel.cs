// WorldLightModel.cs
//
// THE OUTDOOR LIGHT'S HYPOTHESIS TABLES (Stage 10, docs/world-viewer/10-light-sky-fog.md 4.3-4.6, 6.1 rule 4): every constant
// the client's bytes cannot give, held in one place with its evidence grade, so that each is measured in game or ships with
// its label (runtimeState and the log say which). No value here comes from anywhere but the plan's own lists; none is an
// artistic choice. Grades as in the plan: PROVEN_BYTES, PROVEN_SOURCE, MEASURED, INFERRED, INFERRED (public) (taken from
// public documentation alone, not yet checked against the bytes or the game), UNKNOWN. Pure C#, no UnityEngine.
//
// Directions are in the game's world frame (X north, Y west, Z up; WorldToUnity.Direction turns them into Unity's), unit
// vectors pointing FROM the scene TOWARDS the light or the sun (the n . l convention).
//   the light direction  (4.5) the directional light does not follow the sun: it comes from the north-west at 37 deg
//                        elevation at 00:00 and 12:00 and at 20 deg at 06:00 and 18:00, linear between (two agreeing public
//                        sources; MEASURED within 2.5 deg at three times in step 1 -- azimuth 44.5-46.3 deg, elevation 38-39
//                        at noon, 23.7 at 06:49, 20.9 at 18:46 -- the mapping report's section 4). A LightParams' 0x200
//                        override (SunPolar, SunAzimuth) is recorded, not applied: its angle convention is UNKNOWN.
//   the sun path         (4.4) polar angle 100 deg at 06:00, 5 deg from 11:55 to 12:05, 100 deg at 19:00; a fixed azimuth
//                        towards the north-west (INFERRED (public), one source). Linear between the listed keys (INFERRED);
//                        before 06:00 and after 19:00 held at 100 deg, below the horizon (UNKNOWN: the plan lists no key there).
//   the fog              (4.6, 6.9; step 7: WorldFog.cs) every constant and rule of the fog's record, one row each, with its
//                        grade, its state (applied, held, not applied: the data as read, or arm only: a -wmvFogClassic rule)
//                        and, for a fall-back, its order (per key, or the sky's order for the sun tint's inputs). No row names
//                        a map by number and none carries a list of values.
//   the ring table       (4.3) the sky's colour dome (step 4): SkyDome.cs builds it; Rings lists each ring's elevation as
//                        that construction gives it, with its grade.
//   the sky              (4.0, 4.3) the sky program's constant and the hypotheses that feed it (SkyDome.cs).
//   the terrain light    (4.0, 4.5, 6.10, 6.15) the terrain program's constants and the terms held neutral (TerrainLight.cs).
//   the model light      (2.2, 6.6, 6.7, 6.15; step 6) the M2 and world-model programs' constants, the bases, the variant
//                        choices and the terms held neutral on doodads and exterior world-model groups (ModelLight.cs).

using System;
using System.Collections.Generic;

namespace Wmv.Wow.World
{
    /// <summary>One constant with its evidence grade and where it comes from; the fog's rows (step 7) also carry their state
    /// and, for a fall-back, its order ("" elsewhere).</summary>
    public sealed class GradedValue
    {
        public readonly string Name, Grade, Source;
        public readonly double Value;
        /// <summary>WorldLightModel.StateApplied, StateHeld, StateNotApplied or StateArmOnly ("" for the rows before step 7).</summary>
        public readonly string State;
        /// <summary>WorldLightModel.OrderPerKey or OrderSky for a fall-back ("" otherwise).</summary>
        public readonly string Order;

        public GradedValue(string name, double value, string grade, string source) : this(name, value, grade, source, "", "")
        {
        }

        public GradedValue(string name, double value, string grade, string source, string state, string order = "")
        {
            Name = name;
            Value = value;
            Grade = grade;
            Source = source;
            State = state ?? "";
            Order = order ?? "";
        }

        public override string ToString()
        {
            return Name + " " + Value.ToString("R", System.Globalization.CultureInfo.InvariantCulture) + " (" + Grade + ", " + Source + ")" +
                   (State.Length > 0 ? " [" + State + (Order.Length > 0 ? ", " + Order : "") + "]" : "");
        }
    }

    public static class WorldLightModel
    {
        public const string ProvenBytes = "PROVEN_BYTES", ProvenSource = "PROVEN_SOURCE", Measured = "MEASURED", Inferred = "INFERRED",
                            InferredPublic = "INFERRED (public)", Unknown = "UNKNOWN";

        public const double SecondsPerDay = 86400.0;

        // ---- the light direction (plan 4.5)

        /// <summary>The hours of the light direction table and the elevation (degrees) at each: 37 at 00:00 and 12:00, 20 at
        /// 06:00 and 18:00, linear between; 24:00 is 00:00 again.</summary>
        public static readonly double[] LightDirectionHours = { 0.0, 6.0, 12.0, 18.0, 24.0 };
        public static readonly double[] LightDirectionElevationDeg = { 37.0, 20.0, 37.0, 20.0, 37.0 };
        /// <summary>The table's grade: two agreeing public sources, MEASURED in step 1 within 2.5 deg of it at three times
        /// (11:53-11:58, 06:49, 18:46; the mapping report, section 4 and D1).</summary>
        public const string LightDirectionGrade = Measured;
        public const string LightDirectionInterpolationGrade = InferredPublic;   // "linear between", as listed

        /// <summary>The azimuth the light comes from: north-west, the bisector of game north (+X) and west (+Y) -- MEASURED (44.5-46.3
        /// deg at the same three times). The sun path takes the same azimuth; that is part of SunPathGrade (INFERRED (public)).</summary>
        public const string AzimuthGrade = Measured;

        // ---- the sun path (plan 4.4)

        /// <summary>The hours of the sun path's keys and the sun's polar angle (degrees from the zenith) at each: 100 at 06:00,
        /// 5 from 11:55 to 12:05, 100 at 19:00.</summary>
        public static readonly double[] SunPathHours = { 6.0, 11.0 + 55.0 / 60.0, 12.0 + 5.0 / 60.0, 19.0 };
        public static readonly double[] SunPathPolarDeg = { 100.0, 5.0, 5.0, 100.0 };
        public const string SunPathGrade = InferredPublic;                    // one public source; not checked in game (T4)
        public const string SunPathInterpolationGrade = Inferred;             // linear between the listed keys
        public const string SunPathOutsideGrade = Unknown;                    // held at the end keys' 100 deg outside 06:00-19:00

        // ---- settings

        /// <summary>The game far clip the fog will use (yards; -wmvGameFarclip): the reference sessions' setting. The clients'
        /// settings files written after the sessions hold 7,000 (PROVEN_BYTES, plan 9.1); that it held during the shots is
        /// INFERRED. A setting of the game, not a rendering constant.</summary>
        public const double DefaultFarClip = 7000.0;
        public const string DefaultFarClipGrade = ProvenBytes;

        // ---- the fog (plan 4.6, 6.9; step 7: WorldFog.cs)

        /// <summary>The fog rows' states: applied (drawn by default), held (an input of the program fed one value), not applied (the
        /// data as read), arm only (a rule of -wmvFogClassic, never compared with the game).</summary>
        public const string StateApplied = "applied", StateHeld = "held", StateNotApplied = "not applied", StateArmOnly = "arm only";
        /// <summary>A fall-back's order: resolved on each key before the blend, or on the blended state as the sky resolves it.</summary>
        public const string OrderPerKey = "per key", OrderSky = "the sky's order";

        public static readonly GradedValue[] Fog =
        {
            // the program's literals
            new GradedValue("end fade slope sat(slope (1 - d / end))", WorldFog.EndFade, ProvenBytes,
                            "terrain 2977247 p_1:190 (dx_6_0 literal 0x3FF6DB6DC0000000); the same tail in the M2 and world-model programs", StateApplied),
            new GradedValue("polynomial path lead before the end ratio", WorldFog.ArtLead, ProvenBytes, "terrain 2977247 p_1:211", StateApplied),
            new GradedValue("polynomial path fade slope", WorldFog.ArtFade, ProvenBytes, "terrain 2977247 p_1:225 (dx_6_0 literal 0x400AAAAAA0000000)",
                            StateApplied),
            // the record on the Retail pattern
            new GradedValue("fog start cap (yd): start = min(far clip, cap) x FogScaler", WorldFog.StartCap, WorldFog.StartCapGrade,
                            "plan 4.6, one source; the references' far bands fog more than this start predicts (MEASURED, preliminary; start and " +
                            "density not separated); class C until in-game test T7", StateApplied),
            new GradedValue("FogDensity scale per yard: k = FogDensity x scale", WorldFog.DensityScale, WorldFog.DensityScaleGrade,
                            "plan 4.6, one source; not contradicted near a start of 300 yd (a start-density ridge, preliminary); class C until T7",
                            StateApplied),
            new GradedValue("fog end = the game far clip (1 = yes; FogEnd is not used on this pattern)", 1.0, InferredPublic,
                            "plan 4.6; the low far clip of T7 measures it", StateApplied),
            new GradedValue("keys without FogDensity and FogEnd as read: k 0, the end fade only (1 = yes)", 1.0, Inferred,
                            "no derived density and no no-fog switch; counted per set", StateApplied),
            new GradedValue("fog colour = SkyFogColor (1 = yes)", 1.0, InferredPublic,
                            "plan 4.6; of the single table colours only SkyFogColor fits the far bands (MEASURED, preliminary)", StateApplied),
            new GradedValue("the table colours uploaded as byte / 255 (1 = yes)", 1.0, Inferred,
                            "the programs decode no fog colour (PROVEN_BYTES); step 1's light colours (MEASURED); T7 measures the fog colour",
                            StateApplied),
            new GradedValue("height plane: world height - FogHeight, FogHeight as read (1 = yes)", 1.0, InferredPublic, "plan 4.6", StateApplied),
            new GradedValue("height cubic order: the stored coefficients reversed (1 = yes)", 1.0, Inferred,
                            "the data's pattern: poly(0) near 0 on 92.2 % of the keys read constant first, 14.4 % reversed (census)", StateApplied),
            // the fall-backs only the record uses: resolved per key (WorldFog.DeriveKey)
            new GradedValue("colour fall-back: EndFogColor zero -> SkyFogColor (1 = applied)", 1.0, InferredPublic,
                            "the public rule; 55.5 % of Retail keys store it zero (census)", StateApplied, OrderPerKey),
            new GradedValue("colour fall-back: FogHeightColor zero -> SkyFogColor (1 = applied)", 1.0, InferredPublic,
                            "the public rule; 54.3 % of Retail keys store it zero (census)", StateApplied, OrderPerKey),
            new GradedValue("colour fall-back: EndFogHeightColor zero -> the key's resolved EndFogColor (1 = applied)", 1.0, InferredPublic,
                            "the public rule; 76.2 % of Retail keys store it zero (census)", StateApplied, OrderPerKey),
            new GradedValue("height-coefficient fall-back: all four zero -> u^3 (1 = applied)", 1.0, InferredPublic,
                            "the public rule; bites on 6639 Retail keys, 307 of them without exponential fog as read (census); class C until a capture",
                            StateApplied, OrderPerKey),
            new GradedValue("height density fall-back: kh := FogDensity where FogHeightScaler is 0 (1 = applied)", 1.0, InferredPublic,
                            "the public rule; acts on 21 Retail keys (census)", StateApplied, OrderPerKey),
            new GradedValue("colour ramp length fall-back: EndFogColorDistance <= 0 -> the game far clip (1 = applied)", 1.0, InferredPublic,
                            "the public rule simplified: no far-clip clamp, no per-map minima; acts on 356 Retail keys (census)", StateApplied,
                            OrderPerKey),
            new GradedValue("colour ramp length: the second public reading for EndFogColorDistance <= 0 (yd)", 1000.0, InferredPublic,
                            "the public packing; recorded, not drawn", StateNotApplied),
            new GradedValue("polynomial path switch: on iff a coefficient is set and MainFogEndDist > MainFogStartDist (1 = applied)", 1.0,
                            InferredPublic, "the public rule; 34 Retail keys, 2 of them refused and drawn without the path (census)", StateApplied,
                            OrderPerKey),
            new GradedValue("polynomial path: a blended end not above its start is drawn without the path that frame (1 = applied)", 1.0, Inferred,
                            "only while blending with a refused key; counted", StateApplied),
            new GradedValue("polynomial path: the forced end-start separation (yd)", 0.001, InferredPublic,
                            "the public rule; a key with end <= start draws without the path instead", StateNotApplied),
            new GradedValue("coefficients: the zero epsilon of the public rule", 1.2e-7, InferredPublic,
                            "the coefficients are tested exactly 0 as read", StateNotApplied),
            // the sun tint's inputs: the sky's (SkyDome.SunTint)
            new GradedValue("sun tint fall-back: SunFogColor zero -> threshold 1, no tint (1 = applied)", 1.0, InferredPublic,
                            "the sky's rule since step 4 (SkyDome.SunTint), on the blended state", StateApplied, OrderSky),
            new GradedValue("sun tint strength factor SunAngleBlend", SkyDome.SunAngleBlend, SkyDome.SunAngleBlendGrade,
                            "the sky's (SkyDome.SunTint)", StateApplied, OrderSky),
            new GradedValue("sun tint day curve: the sky's (1 = shared)", 1.0, SkyDome.DayCurveGrade, "SkyDome.DayCurve; in-game test T14",
                            StateApplied, OrderSky),
            new GradedValue("sun direction: the sky's sun path (1 = shared)", 1.0, SunPathGrade, "WorldLightModel.SunDirection; in-game tests T4, T14",
                            StateApplied),
            new GradedValue("LightParams flag 0x4: sun tint threshold", WorldFog.ParamsFlagTintAngle, InferredPublic,
                            "the public rule; not applied, as the sky does not apply it; counted per set", StateNotApplied),
            // the holds
            new GradedValue("second start term (held)", 0.0, Unknown,
                            "the program's neutral value; its source unknown (LightData Field 042 and 043 are candidates)", StateHeld),
            new GradedValue("layer weight, one layer (held)", 1.0, Unknown, "neutral for one layer (plan 6.9: layer 0 only)", StateHeld),
            new GradedValue("M2 and world-model fog strength cb0[23].z (held)", 1.0, Unknown,
                            "the terrain's compiled-in value (2977247 p_1:234-235), not a neutral one; per material unknown", StateHeld),
            new GradedValue("interior weight (held: the outdoor record)", 0.0, Inferred,
                            "outdoor doodads and exterior groups; no MOCV stream is uploaded (Stage 12)", StateHeld),
            new GradedValue("the two-record path and the fog-volume composite (held off)", 0.0, Unknown, "Stage 12 or later (D4)", StateHeld),
            // the fog targets and the skip (the CPU's choices)
            new GradedValue("fog target of additive batches: black, rgb x vis (kind 1)", 1.0, Inferred,
                            "the contribution fades as the blend word 4 fades it; a fog-colour mix would add fog light", StateApplied),
            new GradedValue("fog target of modulate batches: white (kind 2)", 1.0, InferredPublic,
                            "the PTR's volume composite uses white for its fog-mode word 2 (PROVEN_BYTES); the mode-to-word map is not in the bytes",
                            StateApplied),
            new GradedValue("fog target of modulate 2x batches: grey (kind 3)", 0.5, InferredPublic,
                            "the PTR's volume composite uses grey 0.5 for its fog-mode word 3 (PROVEN_BYTES)", StateApplied),
            new GradedValue("fog of One / OneMinusSrcAlpha batches: rgba x vis (kind 4)", 1.0, Inferred,
                            "the client's blend word 4 arithmetic exactly (2977063 p_1:586-589); which batches get the word is the CPU's", StateApplied),
            new GradedValue("NoFog (M2 0x02) and F_UNFOGGED (MOMT 0x02) select the program's fog skip (1 = applied)", 1.0, InferredPublic,
                            "the skip returns the lit colour (PROVEN_BYTES); which flag sets it is the public reading", StateApplied),
            new GradedValue("fog mode by client: the Retail pattern for wow and wowt, off for every other product (1 = applied)", 1.0, Inferred,
                            "D17, D18; the plan's 6.9 departure (c)", StateApplied),
            // step 3's rows, kept: the data as read
            new GradedValue("FogScaler clamp (start = ... x clamp(FogScaler, -1, 1))", 1.0, InferredPublic, "plan 4.6, one source; data as read",
                            StateNotApplied),
            new GradedValue("FogDensity floor (k = max(FogDensity, floor) x scale)", WorldFog.DensityFloor, InferredPublic,
                            "plan 4.6, one source; data as read", StateNotApplied),
            // the public arm (-wmvFogClassic=public; WorldFog.Public*)
            new GradedValue("public arm rule 1: the density under Map Flags[2] 0x2", WorldFog.FlagDensity, InferredPublic, "one public source",
                            StateArmOnly),
            new GradedValue("public arm rule 2: the derived density of keys without FogDensity, its base", WorldFog.DerivedDensityBase, InferredPublic,
                            "one public source: base + 5.5 (1 - diff / (min(far clip, 700) - 200)), diff = FogEnd - FogEnd x FogScaler", StateArmOnly),
            new GradedValue("public arm rule 3: the density floor", WorldFog.DensityFloor, InferredPublic, "one public source", StateArmOnly),
            new GradedValue("public arm rule 4: the FogScaler clamp to [-1, 1] and its floors -0.2 with FogDensity, 0 without", WorldFog.ScalerClamp,
                            InferredPublic, "one public source; the floors not under Map Flags[2] 0x2", StateArmOnly),
            new GradedValue("public arm rule 5: the FogEnd floor", WorldFog.FogEndFloor, InferredPublic, "one public source", StateArmOnly),
            new GradedValue("public arm rule 6: FogHeight above the limit -> 0, below its negative -> the negative", WorldFog.FogHeightOffAbove,
                            InferredPublic, "one public source", StateArmOnly),
            new GradedValue("public arm rule 7: the far-clip clamp, its lower end (yd; upper 50000)", WorldFog.FarClipMin, InferredPublic,
                            "one public source", StateArmOnly),
            // the power36 arm (-wmvFogClassic=power36)
            new GradedValue("power36 arm: end = FogEnd / divisor", WorldFog.PowerDivisor, WorldFog.PowerGrade,
                            "plan 4.6: the public sources disagree (in-game test T8)", StateArmOnly),
            new GradedValue("power36 arm: start = FogScaler x end (1 = yes)", 1.0, Unknown, "a public renderer", StateArmOnly),
            new GradedValue("power36 arm: rate = the derived density of public arm rule 2 with end for FogEnd (1 = yes)", 1.0, Unknown,
                            "a public renderer", StateArmOnly),
            new GradedValue("power36 arm: SkyFogColor for all four colours, no tint (1 = yes)", 1.0, Unknown, "a public renderer",
                            StateArmOnly),
            new GradedValue("power36 arm: no fog where the end is not above the start (1 = yes)", 1.0, Unknown, "counted", StateArmOnly),
        };

        // ---- the colour dome's rings (plan 4.3; step 4: SkyDome.cs, table A, the construction's elevations)

        public static readonly GradedValue[] Rings = RingTable();

        static GradedValue[] RingTable()
        {
            var r = new double[SkyDome.ProfilePoints];
            var h = new double[SkyDome.ProfilePoints];
            SkyDome.Profile(false, r, h);
            string[] names = { "SkyMiddle", "SkyBand1", "SkyBand2", "SkySmog", "SkyFog (or its lean to EndFog)" };
            var t = new GradedValue[SkyDome.RingCount];
            for (int k = 1; k <= SkyDome.RingCount; k++)
                t[k - 1] = new GradedValue("ring " + k + " elevation (deg), " + names[k - 1] + ", table A", SkyDome.ElevationDeg(r[k], h[k]),
                                           k <= 3 ? SkyDome.RingsOneToThreeGrade : SkyDome.RingsFourFiveGrade,
                                           k <= 3 ? "plan 4.3: the public table, fitted to about 0.3 deg on the Stage 09 PTR shots (step 1)"
                                                  : "plan 4.3: the public table A; table B differs (-wmvSkyTable=b); open until test T1");
            return t;
        }

        // ---- the sky program and its inputs (plan 4.0, 4.3; step 4: SkyDome.cs)

        public static readonly GradedValue[] Sky =
        {
            new GradedValue("horizon ramp sat(k z) of the raw table height, k", SkyDome.HorizonRamp, SkyDome.HorizonRampGrade, "plan 4.0, sky program 2977085"),
            new GradedValue("tint strength factor SunAngleBlend", SkyDome.SunAngleBlend, SkyDome.SunAngleBlendGrade, "mapping report 1, one source"),
            new GradedValue("tint day curve: 0 until (min)", SkyDome.DayCurveMinutes[0], SkyDome.DayCurveGrade, "mapping report 1, one source"),
            new GradedValue("tint day curve: 1 from (min)", SkyDome.DayCurveMinutes[1], SkyDome.DayCurveGrade, "mapping report 1, one source"),
            new GradedValue("tint day curve: 1 until (min)", SkyDome.DayCurveMinutes[2], SkyDome.DayCurveGrade, "mapping report 1, one source"),
            new GradedValue("tint day curve: 0 from (min)", SkyDome.DayCurveMinutes[3], SkyDome.DayCurveGrade, "mapping report 1, one source"),
            new GradedValue("ring 5 lean to EndFogColor: sat(far clip / EndFogColorDistance) (1 = the rule applies)", 1.0, SkyDome.FogLeanGrade,
                            "mapping report 1, one source (-wmvSkyEndFog=off: 0); open until test T1"),
            new GradedValue("dither (cb1[3].x)", 0.0, Unknown,
                            "off by choice (mapping report 5): what the client sets is not known; the program's blue-noise dither is not drawn"),
        };

        // ---- the terrain light (plan 4.0, 4.5, 6.10, 6.15; step 5: TerrainLight.cs)

        public static readonly GradedValue[] Terrain =
        {
            new GradedValue("wrap base", TerrainOutdoorLight.WrapBase, TerrainOutdoorLight.WrapGrade, "terrain program 2977247; MEASURED 0.710"),
            new GradedValue("wrap scale", TerrainOutdoorLight.WrapScale, TerrainOutdoorLight.WrapGrade, "terrain program 2977247; MEASURED slope 0.198"),
            new GradedValue("wrap half (0.5 + 0.5 N.L)", TerrainOutdoorLight.WrapHalf, TerrainOutdoorLight.WrapGrade, "terrain program 2977247"),
            new GradedValue("albedo vertex colour factor (x MCCV byte / 255)", TerrainOutdoorLight.VertexColourFactor, ProvenBytes,
                            "terrain programs; MEASURED scale 0.990-1.009"),
            new GradedValue("vertex colour of a chunk without MCCV", TerrainOutdoorLight.NoVertexColour, Inferred, "terrain vertex program 2977850 (cb3[9].x)"),
            new GradedValue("MCNR scale (byte / k)", TerrainMcnr.Scale, TerrainMcnr.ScaleGrade, "plan 2.7; the axis fit MEASURED (1.95 deg)"),
            new GradedValue("flat variant shadow base (for the record)", TerrainOutdoorLight.FlatShadowBase, ProvenBytes, "terrain program 2977247 (flat class)"),
            new GradedValue("flat variant shadow scale (for the record)", TerrainOutdoorLight.FlatShadowScale, ProvenBytes, "terrain program 2977247 (flat class)"),
            new GradedValue("attenuation (held)", TerrainOutdoorLight.Attenuation, Measured, "mapping report 2: consistent with the scale k = 1"),
            new GradedValue("shadow (held: no cast shadows)", TerrainOutdoorLight.Shadow, Measured,
                            "mapping report 2: right at noon; the dawn and dusk shots show cast shadows (D10, plan 6.15)"),
            new GradedValue("local lights (held)", TerrainOutdoorLight.Local, Measured, "mapping report 2: consistent with k = 1"),
            new GradedValue("second directional light (held)", TerrainOutdoorLight.SecondLight, Measured, "mapping report 2: no residual needs it"),
            new GradedValue("specular (held: its scale is unknown)", TerrainOutdoorLight.Specular, Unknown,
                            "mapping report 2: DirectColor's hue MEASURED, the scale x layer alpha about 0.12"),
        };

        // ---- the model light (plan 2.2, 6.6, 6.7, 6.15; step 6: ModelLight.cs, doodads and exterior world-model groups)

        public static readonly GradedValue[] Models =
        {
            new GradedValue("model wrap base", ModelOutdoorLight.WrapBase, ModelOutdoorLight.WrapGrade, "M2 2977063 p_1:219-222; world model 5446718 p_1:185-188"),
            new GradedValue("model wrap scale", ModelOutdoorLight.WrapScale, ModelOutdoorLight.WrapGrade, "M2 2977063 p_1:219-222; world model 5446718 p_1:185-188"),
            new GradedValue("model wrap half (0.5 + 0.5 N.L, N.L not saturated)", ModelOutdoorLight.WrapHalf, ModelOutdoorLight.WrapGrade,
                            "M2 2977063 p_1:219-222; world model 5446718 p_1:185-188"),
            new GradedValue("H and the wrap on the normalised normal (1 = yes)", 1.0, ProvenBytes, "M2 2977063 p_1:186-190; world model 5446718 p_1:152-156"),
            new GradedValue("direct term on the raw interpolated normal, saturated (1 = yes)", 1.0, ProvenBytes, "M2 2977063 p_1:223; world model 5446718 p_1:189"),
            new GradedValue("vertex normal normalised per vertex (1 = yes)", 1.0, ProvenBytes, "M2 vertex 2977756 v_1:61-72; world model 5446713 p_1:73-78"),
            new GradedValue("combine sqrt((base F)^2 + base^2 local): local light (held)", ModelOutdoorLight.Local, Measured,
                            "M2 2977063 p_1:278-279, world model 5446718 p_1:247-248; consistent with k about 1 on the back set (measure plan 2.3); placed lights are Stage 12"),
            new GradedValue("M2 base factor (combiner x _Color, nothing added)", ModelOutdoorLight.M2BaseFactor, ProvenBytes,
                            "diffuse_* vertex families 2977756 v_1:57-58, v_5:57 (COLOR0 = 0.5 x colour), pixel x 2 2977063 p_1:180-181; the family per batch INFERRED (public)"),
            new GradedValue("world-model base factor, red and blue (2 x 0.5)", ModelOutdoorLight.WmoGateOffRed, ProvenBytes,
                            "gate-off COLOR0 5446713 p_1:89, 2977862 p_1:89, pixel x 2 5446718 p_1:146-147; the gate off for exterior batches INFERRED"),
            new GradedValue("world-model base factor, green (2 x 0.49999)", ModelOutdoorLight.WmoGateOffGreen, ProvenBytes,
                            "gate-off COLOR0 5446713 p_1:89, 2977862 p_1:89, pixel x 2 5446718 p_1:146-147; the gate off for exterior batches INFERRED"),
            new GradedValue("doodad variant (0 hemisphere, 1 flat)", 0.0, Measured,
                            "measure plan 2.3: fits with no free term at three independent times; flat excluded only at K = 0; the trunk blue slope decides on the render"),
            new GradedValue("world-model variant (0 hemisphere, 1 flat)", 0.0, Inferred,
                            "the same scene block and classes as the doodads and the terrain; open until a capture with an exterior world model"),
            new GradedValue("modulate M2 batches (Mod, Mod2x) lit by F (1 = yes, as the builder classifies them)", 1.0, Unknown,
                            "the client's lighting mode for them is set on the CPU; a labelled choice, counted per blend mode"),
            new GradedValue("additive M2 batches without flag 0x01 unlit (1 = yes, today's choice)", 1.0, Unknown,
                            "the client's lighting mode for them is set on the CPU; lighting mode 0 returns the base (PROVEN_BYTES)"),
            new GradedValue("attenuation (held)", ModelOutdoorLight.Attenuation, Measured, "consistent with k about 1 on the back set (measure plan 2.3)"),
            new GradedValue("shadow (held; on D only)", ModelOutdoorLight.Shadow, Measured,
                            "placement PROVEN_BYTES (2977063 p_203:199-212, 5446718 p_17:164-176); right on back-facing pixels, wrong on light-facing ones " +
                            "at dawn and dusk: cast shadows not drawn (D10, plan 6.15)"),
            new GradedValue("ambient occlusion (held)", ModelOutdoorLight.Ao, Measured,
                            "jointly with the base, the scene colours and the secondary light: k about 1 on the back set; SSAO not drawn"),
            new GradedValue("secondary light G (PTR; held)", 0.0, Measured, "jointly, as the ambient occlusion; Retail has none (PROVEN_BYTES)"),
            new GradedValue("second directional term (held)", ModelOutdoorLight.SecondLight, Measured, "consistent with k about 1; its source UNKNOWN"),
            new GradedValue("override weight (held)", ModelOutdoorLight.OverrideWeight, Inferred,
                            "outdoor context: exterior MOCV alpha 255 on 99.993 % of vertices (groups that carry MOCV only); no data for the rest and for M2"),
            new GradedValue("world-model MOCV light E (deferred)", ModelOutdoorLight.MocvLight, Unknown,
                            "its k and per-batch mode are set on the CPU; up to about +0.5 x albedo at the p95 if mode 2 with k = 1 (plan 2.2)"),
            new GradedValue("world-model per-material colour lerp (held)", 0.0, Unknown, "5446718 p_1 cb0[17]; its source UNKNOWN"),
            new GradedValue("M2 glow (held)", ModelOutdoorLight.Glow, Unknown, "2977063 p_1:281-300 (cb0[16].w); not drawn today"),
            new GradedValue("M2 desaturation (held)", ModelOutdoorLight.Desaturation, Unknown, "2977063 p_1:303-308 (cb0[17].w); not drawn today"),
            new GradedValue("flat class K (cb8[9]; the flat arms only)", ModelOutdoorLight.FlatK, Unknown, "the flat class's rejection holds at K = 0 only"),
            new GradedValue("M2 specular", 0.0, ProvenBytes, "0 of 2,688 lit M2 programs read cb8[7]"),
        };

        /// <summary>Every constant above in one list (the log's).</summary>
        public static IList<GradedValue> Table()
        {
            var t = new List<GradedValue>();
            for (int k = 0; k < LightDirectionHours.Length; k++)
                t.Add(new GradedValue("light elevation (deg) at " + LightDirectionHours[k] + " h", LightDirectionElevationDeg[k], LightDirectionGrade,
                                      "plan 4.5, two sources; within 2.5 deg at three times in step 1"));
            for (int k = 0; k < SunPathHours.Length; k++)
                t.Add(new GradedValue("sun polar angle (deg) at " + SunPathHours[k].ToString("0.###", System.Globalization.CultureInfo.InvariantCulture) + " h",
                                      SunPathPolarDeg[k], SunPathGrade, "plan 4.4, one source"));
            t.Add(new GradedValue("game far clip (yd)", DefaultFarClip, DefaultFarClipGrade, "plan 9.1, the reference sessions' settings files"));
            t.AddRange(Fog);
            t.AddRange(Rings);
            t.AddRange(Sky);
            t.AddRange(Terrain);
            t.AddRange(Models);
            return t;
        }

        /// <summary>The value of a piecewise-linear table at x (keys ascending); held at the end values outside them.</summary>
        public static double Linear(double[] keys, double[] values, double x)
        {
            if (x <= keys[0])
                return values[0];
            for (int k = 1; k < keys.Length; k++)
                if (x <= keys[k])
                    return values[k - 1] + (values[k] - values[k - 1]) * ((x - keys[k - 1]) / (keys[k] - keys[k - 1]));
            return values[values.Length - 1];
        }

        static double Hours(double secondsOfDay)
        {
            double s = secondsOfDay % SecondsPerDay;
            if (s < 0.0)
                s += SecondsPerDay;
            return s / 3600.0;
        }

        /// <summary>The light's elevation (degrees) at a time of day (seconds; wrapped into the day).</summary>
        public static double LightElevationDeg(double secondsOfDay)
        {
            return Linear(LightDirectionHours, LightDirectionElevationDeg, Hours(secondsOfDay));
        }

        /// <summary>The sun's polar angle (degrees from the zenith) at a time of day (seconds; wrapped into the day).</summary>
        public static double SunPolarDeg(double secondsOfDay)
        {
            return Linear(SunPathHours, SunPathPolarDeg, Hours(secondsOfDay));
        }

        /// <summary>The unit vector towards the light (game frame) at a time of day: from the north-west at the table's
        /// elevation.</summary>
        public static void LightDirection(double secondsOfDay, out double x, out double y, out double z)
        {
            double e = LightElevationDeg(secondsOfDay) * Math.PI / 180.0;
            double h = Math.Cos(e) * Math.Sqrt(0.5);
            x = h;                  // north
            y = h;                  // west
            z = Math.Sin(e);
        }

        /// <summary>The unit vector towards the sun (game frame) at a time of day: towards the north-west at the path's polar
        /// angle (below the horizon when it exceeds 90 deg).</summary>
        public static void SunDirection(double secondsOfDay, out double x, out double y, out double z)
        {
            double p = SunPolarDeg(secondsOfDay) * Math.PI / 180.0;
            double h = Math.Sin(p) * Math.Sqrt(0.5);
            x = h;
            y = h;
            z = Math.Cos(p);
        }
    }
}
