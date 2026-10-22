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
//   the fog constants    (4.6) carried for the fog step, read by nothing yet.
//   the ring table       (4.3) the sky's colour dome (step 4): SkyDome.cs builds it; Rings lists each ring's elevation as
//                        that construction gives it, with its grade.
//   the sky              (4.0, 4.3) the sky program's constant and the hypotheses that feed it (SkyDome.cs).
//   the terrain light    (4.0, 4.5, 6.10, 6.15) the terrain program's constants and the terms held neutral (TerrainLight.cs).

using System;
using System.Collections.Generic;

namespace Wmv.Wow.World
{
    /// <summary>One constant with its evidence grade and where it comes from.</summary>
    public sealed class GradedValue
    {
        public readonly string Name, Grade, Source;
        public readonly double Value;

        public GradedValue(string name, double value, string grade, string source)
        {
            Name = name;
            Value = value;
            Grade = grade;
            Source = source;
        }

        public override string ToString()
        {
            return Name + " " + Value.ToString("R", System.Globalization.CultureInfo.InvariantCulture) + " (" + Grade + ", " + Source + ")";
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

        // ---- the fog constants (plan 4.6; read by nothing in step 3)

        public static readonly GradedValue[] Fog =
        {
            new GradedValue("fog start cap (yd): start = min(far clip, cap) x FogScaler", 3000.0, InferredPublic, "plan 4.6, one source"),
            new GradedValue("FogScaler clamp (start = ... x clamp(FogScaler, -1, 1))", 1.0, InferredPublic, "plan 4.6, one source"),
            new GradedValue("FogDensity floor (k = max(FogDensity, floor) x scale)", 0.9, InferredPublic, "plan 4.6, one source"),
            new GradedValue("FogDensity scale per yard", 0.0005, InferredPublic, "plan 4.6, one source"),
            new GradedValue("end fade slope sat(slope (1 - d / end))", 1.4286, ProvenBytes, "plan 4.0, the client's terrain and M2 programs"),
            new GradedValue("Era/MoP power fog: end = FogEnd / divisor", 36.0, Unknown, "plan 4.6: the public sources disagree (T8)"),
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
