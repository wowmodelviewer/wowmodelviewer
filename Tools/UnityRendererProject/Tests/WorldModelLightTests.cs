// WorldModelLightTests.cs
//
// Unit tests of Stage 10 step 6 (docs/world-viewer/10-light-sky-fog.md 2.2, 6.6, 6.7, 6.15): the outdoor light on doodads and
// exterior world-model groups (Assets/Scripts/Wow/World/ModelLight.cs) and the context rule that decides which world renderers
// take it.
//
//   1. The client's own programs as the target: ModelOutdoorLight.ShadeM2 against the client's M2 hemisphere program
//      (combiners_opaque 2977063 p_1) and flat program (p_195, K = 0), executed offline at the Southfury light (Retail
//      LightParams 5985 at 11:57, 06:49 and 18:46: the colours as bytes / 255, the light from the north-west at 36.9, 22.3
//      and 22.2 deg) on nine normals and two albedos -- 54 rows each, embedded below to the fixture's four decimals (the
//      fixture file's SHA-256 is named; the runner reads no file for it).
//   2. With a unit normal the doodad light equals the terrain's (TerrainOutdoorLight.Shade, albedo without MCCV); with every
//      hold at its neutral value Shade equals base x Factor (the sqrt combine reduces exactly).
//   3. A raw interpolated normal shorter than 1: H and the wrap from the normalised vector, the direct term from the raw one.
//   4. The rules: a zero horizon or ground colour falls back to AmbientColor; N.L < 0 gives no direct light and a wrap under
//      0.9; unlit returns the base; shadow 0 removes D only, in the hemisphere and in the flat class (the M2 and world-model
//      placement, not the terrain flat class's 0.7 + 0.3 x shadow); AO scales A only; the local light's sqrt combine.
//   5. The world model: ShadeWmo = ShadeM2 x (1, 0.99998, 1) per channel, and its flat arm.
//   6. The contexts on synthetic world models (WmoSynthetic.ContextRoot / ContextGroup): exterior, interior, both and neither
//      flags; MODD records listed by exterior groups only, interior only, both, none; an incomplete world model; MDDF; the
//      census counts; MddfInsideInterior under a turned, tilted and scaled placement.
//   7. With WMV_WORLD_AUDIT_CORPUS: the census per client of the world07 corpus (printed), and the Retail window of
//      <corpus>/assets/wow (its file set checked against Tools/world-audit/tests/corpus_hashes.json first) against the plan's
//      2.2: 52 complete roots, 531 LOD0 groups (300 exterior, 231 interior), 11,470 MODD records listed, 6 by both kinds.
//      Without it that part reports SKIPPED.
// Framework-free like the other suites: RunAll returns the failure count.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class WorldModelLightTests
    {
        static int failures;
        static readonly List<string> log = new List<string>();

        static void Check(bool condition, string name)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name); failures++; }
        }

        static void Info(string line) { log.Add("  INFO  " + line); }

        static bool Near(double a, double b, double eps = 1e-12) { return Math.Abs(a - b) <= eps; }

        static string F(double v, string fmt = "0.####") { return v.ToString(fmt, CultureInfo.InvariantCulture); }

        // ================================================================================== 1. the client's own programs

        // D:\wmv-10\step6\m2\out\fixture_southfury.tsv (SHA-256 2f391dcf64c7365c9b2fe16984bdb6923eeb4e963a7d2096209c53673e5c7db5),
        // written by D:\wmv-10\step6\m2\scripts\fixture_southfury.py: the Retail M2 programs combiners_opaque 2977063 p_1
        // (hemisphere, scene block cb8) and p_195 (flat, cb8[9] = K = 0) executed with lighting mode 1, the local light,
        // attenuation, second directional term and override weight off, COLOR0 = 0.5 (a white colour track), texture t0 = the
        // albedo. Rows in the file's order: the times (11:57, 06:49, 18:46), in each the albedos (grey 0.5, then bark), in each
        // the nine normals of FixtureNormals. The light's N.L as the file prints it (3 decimals).
        static readonly string[] FixtureTimes = { "11:57", "06:49", "18:46" };
        // Bytes as the step 1 mapping read them from the PTR tables (LightParams 5985): Direct, Ambient, Horizon, Ground.
        static readonly int[][][] FixtureColours =
        {
            new[] { new[] { 94, 50, 0 }, new[] { 183, 165, 184 }, new[] { 156, 138, 157 }, new[] { 158, 120, 101 } },
            new[] { new[] { 116, 79, 0 }, new[] { 156, 156, 159 }, new[] { 119, 116, 119 }, new[] { 117, 112, 110 } },
            new[] { new[] { 120, 62, 0 }, new[] { 132, 107, 124 }, new[] { 130, 105, 121 }, new[] { 130, 103, 115 } },
        };
        static readonly double[] FixtureElevationDeg = { 36.9, 22.3, 22.2 };
        static readonly double[][] FixtureAlbedos = { new[] { 0.5, 0.5, 0.5 }, new[] { 0.45, 0.35, 0.25 } };
        static readonly string[] FixtureNormalNames =
        {
            "up", "down", "vertical, facing the light", "vertical, side", "vertical, facing away", "45 deg up, toward the light",
            "45 deg up, away from the light", "45 deg down, toward the light", "45 deg down, away",
        };

        static readonly double[] FixtureNL =
        {
            0.600, -0.600, 0.800, 0.000, -0.800, 0.990, -0.141, 0.141, -0.990,
            0.600, -0.600, 0.800, 0.000, -0.800, 0.990, -0.141, 0.141, -0.990,
            0.379, -0.379, 0.925, 0.000, -0.925, 0.923, -0.386, 0.386, -0.923,
            0.379, -0.379, 0.925, 0.000, -0.925, 0.923, -0.386, 0.386, -0.923,
            0.378, -0.378, 0.926, 0.000, -0.926, 0.922, -0.388, 0.388, -0.922,
            0.378, -0.378, 0.926, 0.000, -0.926, 0.922, -0.388, 0.388, -0.922,
        };
        // hemi_cb8 (p_1) rgb, three values a row.
        static readonly double[] FixtureHemi =
        {
            0.4767, 0.3889, 0.3680, 0.2416, 0.1835, 0.1545, 0.4716, 0.3652, 0.3263,
            0.2753, 0.2435, 0.2771, 0.2264, 0.2003, 0.2278, 0.5594, 0.4353, 0.3791,
            0.2993, 0.2685, 0.3010, 0.3125, 0.2418, 0.2137, 0.2167, 0.1724, 0.1616,
            0.4290, 0.2722, 0.1840, 0.2175, 0.1285, 0.0772, 0.4244, 0.2556, 0.1631,
            0.2478, 0.1705, 0.1385, 0.2037, 0.1402, 0.1139, 0.5035, 0.3047, 0.1896,
            0.2694, 0.1880, 0.1505, 0.2812, 0.1693, 0.1068, 0.1950, 0.1207, 0.0808,
            0.3848, 0.3573, 0.3042, 0.1891, 0.1810, 0.1777, 0.4636, 0.3901, 0.2532,
            0.2100, 0.2047, 0.2100, 0.1668, 0.1626, 0.1668, 0.5185, 0.4497, 0.3132,
            0.2342, 0.2328, 0.2376, 0.3131, 0.2766, 0.2158, 0.1650, 0.1588, 0.1580,
            0.3463, 0.2501, 0.1521, 0.1702, 0.1267, 0.0889, 0.4173, 0.2731, 0.1266,
            0.1890, 0.1433, 0.1050, 0.1501, 0.1138, 0.0834, 0.4667, 0.3148, 0.1566,
            0.2108, 0.1629, 0.1188, 0.2818, 0.1936, 0.1079, 0.1485, 0.1111, 0.0790,
            0.3414, 0.2506, 0.2372, 0.2101, 0.1665, 0.1859, 0.4945, 0.3360, 0.2575,
            0.2294, 0.1853, 0.2135, 0.1822, 0.1472, 0.1696, 0.4963, 0.3383, 0.2618,
            0.2119, 0.1716, 0.1986, 0.3403, 0.2456, 0.2238, 0.1824, 0.1454, 0.1638,
            0.3073, 0.1754, 0.1186, 0.1891, 0.1166, 0.0930, 0.4450, 0.2352, 0.1287,
            0.2065, 0.1297, 0.1068, 0.1640, 0.1030, 0.0848, 0.4467, 0.2368, 0.1309,
            0.1907, 0.1201, 0.0993, 0.3063, 0.1720, 0.1119, 0.1642, 0.1017, 0.0819,
        };
        // flat_cb8 K=0 (p_195) rgb, three values a row.
        static readonly double[] FixtureFlat =
        {
            0.4695, 0.3824, 0.3608, 0.3588, 0.3235, 0.3608, 0.5000, 0.4019, 0.3608,
            0.3588, 0.3235, 0.3608, 0.3588, 0.3235, 0.3608, 0.5000, 0.4206, 0.3608,
            0.3588, 0.3235, 0.3608, 0.3848, 0.3373, 0.3608, 0.3588, 0.3235, 0.3608,
            0.4225, 0.2677, 0.1804, 0.3229, 0.2265, 0.1804, 0.4500, 0.2814, 0.1804,
            0.3229, 0.2265, 0.1804, 0.3229, 0.2265, 0.1804, 0.4500, 0.2944, 0.1804,
            0.3229, 0.2265, 0.1804, 0.3463, 0.2361, 0.1804, 0.3229, 0.2265, 0.1804,
            0.3922, 0.3647, 0.3118, 0.3059, 0.3059, 0.3118, 0.5000, 0.4492, 0.3118,
            0.3059, 0.3059, 0.3118, 0.3059, 0.3059, 0.3118, 0.5000, 0.4488, 0.3118,
            0.3059, 0.3059, 0.3118, 0.3937, 0.3657, 0.3118, 0.3059, 0.3059, 0.3118,
            0.3530, 0.2553, 0.1559, 0.2753, 0.2141, 0.1559, 0.4500, 0.3144, 0.1559,
            0.2753, 0.2141, 0.1559, 0.2753, 0.2141, 0.1559, 0.4500, 0.3141, 0.1559,
            0.2753, 0.2141, 0.1559, 0.3543, 0.2560, 0.1559, 0.2753, 0.2141, 0.1559,
            0.3477, 0.2557, 0.2431, 0.2588, 0.2098, 0.2431, 0.4767, 0.3224, 0.2431,
            0.2588, 0.2098, 0.2431, 0.2588, 0.2098, 0.2431, 0.4757, 0.3219, 0.2431,
            0.2588, 0.2098, 0.2431, 0.3500, 0.2569, 0.2431, 0.2588, 0.2098, 0.2431,
            0.3130, 0.1790, 0.1216, 0.2329, 0.1469, 0.1216, 0.4290, 0.2257, 0.1216,
            0.2329, 0.1469, 0.1216, 0.2329, 0.1469, 0.1216, 0.4282, 0.2253, 0.1216,
            0.2329, 0.1469, 0.1216, 0.3150, 0.1798, 0.1216, 0.2329, 0.1469, 0.1216,
        };

        static double[] Unit(double x, double y, double z)
        {
            double l = Math.Sqrt(x * x + y * y + z * z);
            return new[] { x / l, y / l, z / l };
        }

        /// <summary>The fixture's nine normals in the game frame (x north, y west, z up), as fixture_southfury.py builds them:
        /// the light comes from azimuth 45 deg counter-clockwise from north.</summary>
        static double[][] FixtureNormalsGame()
        {
            double az = 45.0 * Math.PI / 180.0;
            double tx = Math.Cos(az), ty = Math.Sin(az);
            return new[]
            {
                new[] { 0.0, 0.0, 1.0 }, new[] { 0.0, 0.0, -1.0 }, new[] { tx, ty, 0.0 }, new[] { -ty, tx, 0.0 }, new[] { -tx, -ty, 0.0 },
                Unit(tx, ty, 1.0), Unit(-tx, -ty, 1.0), Unit(tx, ty, -1.0), Unit(-tx, -ty, -1.0),
            };
        }

        static double[] ToUnity(double[] g)
        {
            float x, y, z;
            WorldToUnity.Direction(g[0], g[1], g[2], out x, out y, out z);
            return new double[] { x, y, z };
        }

        static double[] Bytes(int[] b) { return new[] { b[0] / 255.0, b[1] / 255.0, b[2] / 255.0 }; }

        static void FixtureTests()
        {
            double[][] normals = FixtureNormalsGame();
            var rgb = new double[3];
            double worstHemi = 0.0, worstFlat = 0.0, worstNl = 0.0;
            string worstHemiAt = "", worstFlatAt = "";
            int rows = 0;
            for (int t = 0; t < 3; t++)
            {
                double el = FixtureElevationDeg[t] * Math.PI / 180.0, az = 45.0 * Math.PI / 180.0;
                double[] l = ToUnity(new[] { Math.Cos(el) * Math.Cos(az), Math.Cos(el) * Math.Sin(az), Math.Sin(el) });
                double[] direct = Bytes(FixtureColours[t][0]), ambient = Bytes(FixtureColours[t][1]), horizon = Bytes(FixtureColours[t][2]),
                         ground = Bytes(FixtureColours[t][3]);
                for (int a = 0; a < 2; a++)
                    for (int k = 0; k < 9; k++, rows++)
                    {
                        double[] n = ToUnity(normals[k]);
                        int row = (t * 2 + a) * 9 + k;
                        worstNl = Math.Max(worstNl, Math.Abs(n[0] * l[0] + n[1] * l[1] + n[2] * l[2] - FixtureNL[row]));
                        ModelOutdoorLight.ShadeM2(FixtureAlbedos[a], n, l, ambient, horizon, ground, direct, false, rgb);
                        for (int c = 0; c < 3; c++)
                        {
                            double e = Math.Abs(rgb[c] - FixtureHemi[3 * row + c]);
                            if (e > worstHemi) { worstHemi = e; worstHemiAt = FixtureTimes[t] + " " + FixtureNormalNames[k] + " channel " + c; }
                        }
                        ModelOutdoorLight.ShadeM2(FixtureAlbedos[a], n, l, ambient, horizon, ground, direct, true, rgb);
                        for (int c = 0; c < 3; c++)
                        {
                            double e = Math.Abs(rgb[c] - FixtureFlat[3 * row + c]);
                            if (e > worstFlat) { worstFlat = e; worstFlatAt = FixtureTimes[t] + " " + FixtureNormalNames[k] + " channel " + c; }
                        }
                    }
            }
            Check(rows == 54 && FixtureNL.Length == 54 && FixtureHemi.Length == 162 && FixtureFlat.Length == 162 && worstNl <= 5.0e-4,
                  "fixture: 54 rows (3 times x 2 albedos x 9 normals); the normals and the light direction rebuilt here (the game frame through " +
                  "WorldToUnity.Direction) give the file's N.L within its 3 decimals (worst " + F(worstNl, "0.######") + ")");
            Check(worstHemi <= 6.0e-5,
                  "the client's M2 hemisphere program (2977063 p_1, executed at the Southfury light): ShadeM2 reproduces its 162 values within " +
                  "the file's 4 decimals (worst " + F(worstHemi, "0.#######") + " at " + worstHemiAt + "; gate 6e-5)");
            Check(worstFlat <= 6.0e-5,
                  "the client's M2 flat program (2977063 p_195, K = 0): the flat arm reproduces its 162 values within the file's 4 decimals " +
                  "(worst " + F(worstFlat, "0.#######") + " at " + worstFlatAt + "; gate 6e-5)");
        }

        // ================================================================================== 2. the terrain's light, and Factor

        static double[] RandomUnit(Random r)
        {
            double u = 2 * r.NextDouble() - 1, phi = 2 * Math.PI * r.NextDouble(), q = Math.Sqrt(1 - u * u);
            return new[] { q * Math.Cos(phi), u, q * Math.Sin(phi) };
        }

        static double[] RandomColour(Random r, bool allowZero)
        {
            if (allowZero && r.NextDouble() < 0.15)
                return new[] { 0.0, 0.0, 0.0 };
            return new[] { r.NextDouble(), r.NextDouble(), r.NextDouble() };
        }

        static void EquivalenceTests()
        {
            var r = new Random(6);
            var m2 = new double[3];
            var terrain = new double[3];
            var shade = new double[3];
            var f = new double[3];
            double worstTerrain = 0.0, worstFlatTerrain = 0.0, worstFactor = 0.0, worstFlatFactor = 0.0;
            for (int k = 0; k < 4000; k++)
            {
                double[] n = RandomUnit(r), l = RandomUnit(r);
                double[] albedo = { 1.6 * r.NextDouble(), r.NextDouble(), r.NextDouble() };
                double[] ambient = RandomColour(r, false), horizon = RandomColour(r, true), ground = RandomColour(r, true), direct = RandomColour(r, false);
                ModelOutdoorLight.ShadeM2(albedo, n, l, ambient, horizon, ground, direct, false, m2);
                TerrainOutdoorLight.Shade(albedo, n, l, ambient, horizon, ground, direct, false, terrain);
                ModelOutdoorLight.Shade(albedo, n, l, ambient, horizon, ground, direct, false, ModelOutdoorLight.Shadow, ModelOutdoorLight.Ao,
                                        ModelOutdoorLight.Local, shade);
                ModelOutdoorLight.Factor(n, l, ambient, horizon, ground, direct, false, ModelOutdoorLight.Shadow, ModelOutdoorLight.Ao, f);
                for (int c = 0; c < 3; c++)
                {
                    worstTerrain = Math.Max(worstTerrain, Math.Abs(m2[c] - terrain[c]));
                    worstFactor = Math.Max(worstFactor, Math.Abs(shade[c] - albedo[c] * f[c]));
                }
                ModelOutdoorLight.ShadeM2(albedo, n, l, ambient, horizon, ground, direct, true, m2);
                TerrainOutdoorLight.Shade(albedo, n, l, ambient, horizon, ground, direct, true, terrain);
                ModelOutdoorLight.Factor(n, l, ambient, horizon, ground, direct, true, ModelOutdoorLight.Shadow, ModelOutdoorLight.Ao, f);
                for (int c = 0; c < 3; c++)
                {
                    worstFlatTerrain = Math.Max(worstFlatTerrain, Math.Abs(m2[c] - terrain[c]));
                    worstFlatFactor = Math.Max(worstFlatFactor, Math.Abs(m2[c] - albedo[c] * f[c]));
                }
            }
            Check(worstTerrain <= 1e-12 && worstFlatTerrain <= 1e-12,
                  "with a unit normal the doodad light equals the terrain's (TerrainOutdoorLight.Shade on an albedo without MCCV), hemisphere " +
                  "and flat class with the shadow held 1, over 4,000 random normals, light directions and colours (zero horizon and ground " +
                  "colours among them; worst " + worstTerrain.ToString("E1", CultureInfo.InvariantCulture) + " / " +
                  worstFlatTerrain.ToString("E1", CultureInfo.InvariantCulture) + ")");
            Check(worstFactor <= 1e-12 && worstFlatFactor <= 1e-12,
                  "with every hold at its neutral value (local 0, AO 1, shadow 1, attenuation 1, second light 0) Shade is exactly base x " +
                  "Factor: sqrt((base F)^2 + 0) = base F (worst " + worstFactor.ToString("E1", CultureInfo.InvariantCulture) + ", flat " +
                  worstFlatFactor.ToString("E1", CultureInfo.InvariantCulture) + ")");
            Check(ModelOutdoorLight.WrapBase == 0.7 && ModelOutdoorLight.WrapScale == 0.4 && ModelOutdoorLight.WrapHalf == 0.5 &&
                  ModelOutdoorLight.Attenuation == 1 && ModelOutdoorLight.Shadow == 1 && ModelOutdoorLight.Ao == 1 && ModelOutdoorLight.Local == 0 &&
                  ModelOutdoorLight.SecondLight == 0 && ModelOutdoorLight.OverrideWeight == 0 && ModelOutdoorLight.MocvLight == 0 &&
                  ModelOutdoorLight.FlatK == 0 && ModelOutdoorLight.Glow == 0 && ModelOutdoorLight.Desaturation == 0 &&
                  ModelOutdoorLight.M2BaseFactor == 1 && ModelOutdoorLight.WrapGrade == "PROVEN_BYTES",
                  "the client's constants (wrap 0.7 / 0.4 / 0.5, PROVEN_BYTES) and Stage 10's holds at their neutral values (attenuation, shadow, " +
                  "AO 1; local, second light, override weight, MOCV light, K, glow, desaturation 0); the M2 base factor 1");
        }

        // ================================================================================== 3. the raw interpolated normal

        static void RawNormalTests()
        {
            // The midpoint of two unit normals 90 deg apart, up (0, 1, 0) and level (1, 0, 0): (0.5, 0.5, 0), 0.7071 long -- what
            // a pixel half-way between two such vertices interpolates. The light (0.6, 0.8, 0).
            var nRaw = new[] { 0.5, 0.5, 0.0 };
            var l = new[] { 0.6, 0.8, 0.0 };
            var ambient = new[] { 0.5, 0.4, 0.3 };
            var horizon = new[] { 0.2, 0.2, 0.2 };
            var ground = new[] { 0.1, 0.0, 0.0 };
            var direct = new[] { 0.6, 0.3, 0.0 };
            var one = new[] { 1.0, 1.0, 1.0 };
            var rgb = new double[3];
            ModelOutdoorLight.ShadeM2(one, nRaw, l, ambient, horizon, ground, direct, false, rgb);
            // By hand: the normalised normal (s, s, 0), s = sqrt(0.5): nn.U = s, nn.L = 0.7 / s = 0.98995 (not saturated), the
            // wrap 0.7 + 0.4 (0.5 + 0.5 x 0.98995) = 1.09799; H = Horizon + (Ambient - Horizon) s; the direct term on the RAW
            // vector: n.L = 0.5 x 0.6 + 0.5 x 0.8 = 0.7, so D = Direct x 0.7 (not 0.98995).
            double s = Math.Sqrt(0.5);
            double wrap = 0.7 + 0.4 * (0.5 + 0.5 * (0.7 / s));
            double[] expect = new double[3];
            for (int c = 0; c < 3; c++)
                expect[c] = (horizon[c] + (ambient[c] - horizon[c]) * s) * wrap + direct[c] * 0.7;
            bool exact = Near(rgb[0], expect[0]) && Near(rgb[1], expect[1]) && Near(rgb[2], expect[2]);
            bool literal = Near(rgb[0], 0.872517, 5e-6) && Near(rgb[1], 0.584877, 5e-6) && Near(rgb[2], 0.297238, 5e-6);
            // The two wrong readings, for the record: everything raw (the terrain's way) and everything normalised.
            double allRaw = (horizon[0] + (ambient[0] - horizon[0]) * 0.5) * (0.7 + 0.4 * (0.5 + 0.5 * 0.7)) + direct[0] * 0.7;
            double allUnit = expect[0] - direct[0] * 0.7 + direct[0] * (0.7 / s);
            Check(exact && literal && Math.Abs(rgb[0] - allRaw) > 0.05 && Math.Abs(rgb[0] - allUnit) > 0.05,
                  "a raw interpolated normal 0.7071 long (the midpoint of up and level): H and the wrap from the NORMALISED vector " +
                  "(nn.U 0.7071, nn.L 0.98995, wrap 1.09799), the direct term from the RAW one (n.L 0.7): (" + F(rgb[0], "0.######") + ", " +
                  F(rgb[1], "0.######") + ", " + F(rgb[2], "0.######") + ") = (0.872517, 0.584877, 0.297238) for a white base -- against " +
                  F(allRaw) + " with everything raw (the terrain's way) and " + F(allUnit) + " with everything normalised (red)");
        }

        // ================================================================================== 4. the rules

        static void RuleTests()
        {
            var one = new[] { 1.0, 1.0, 1.0 };
            var ambient = new[] { 0.5, 0.4, 0.3 };
            var horizon = new[] { 0.2, 0.2, 0.2 };
            var ground = new[] { 0.1, 0.0, 0.0 };
            var zero = new[] { 0.0, 0.0, 0.0 };
            var direct = new[] { 0.6, 0.3, 0.0 };
            var level = new[] { 1.0, 0.0, 0.0 };
            var down = new[] { 0.0, -1.0, 0.0 };
            var up = new[] { 0.0, 1.0, 0.0 };
            var across = new[] { 0.0, 0.0, 1.0 };                      // N.L 0 for level and down normals: A = 0.9 H, D = 0
            var rgb = new double[3];
            ModelOutdoorLight.ShadeM2(one, level, across, ambient, horizon, ground, direct, false, rgb);
            bool h = Near(rgb[0], 0.9 * 0.2);
            ModelOutdoorLight.ShadeM2(one, down, across, ambient, horizon, ground, direct, false, rgb);
            bool g = Near(rgb[0], 0.9 * 0.1) && Near(rgb[1], 0.0);
            ModelOutdoorLight.ShadeM2(one, level, across, ambient, zero, ground, direct, false, rgb);
            bool hz = Near(rgb[0], 0.9 * 0.5) && Near(rgb[2], 0.9 * 0.3);
            ModelOutdoorLight.ShadeM2(one, down, across, ambient, horizon, zero, direct, false, rgb);
            bool gz = Near(rgb[0], 0.9 * 0.5) && Near(rgb[1], 0.9 * 0.4);
            Check(h && g && hz && gz,
                  "H: a level normal takes HorizonAmbientColor, a downward one GroundAmbientColor; a zero horizon or ground colour falls back " +
                  "to AmbientColor (INFERRED (public), the CPU's rule)");

            // N.L < 0: no direct light, the wrap below 0.9 (0.7 at N.L = -1, 0.8 at -0.5).
            ModelOutdoorLight.ShadeM2(one, up, new[] { 0.0, -1.0, 0.0 }, ambient, horizon, ground, direct, false, rgb);
            bool away = Near(rgb[0], 0.5 * 0.7) && Near(rgb[1], 0.4 * 0.7) && Near(rgb[2], 0.3 * 0.7);
            ModelOutdoorLight.ShadeM2(one, up, new[] { Math.Sqrt(0.75), -0.5, 0.0 }, ambient, horizon, ground, direct, false, rgb);
            bool half = Near(rgb[0], 0.5 * 0.8) && Near(rgb[1], 0.4 * 0.8);
            ModelOutdoorLight.ShadeM2(one, up, up, ambient, horizon, ground, direct, false, rgb);
            bool toward = Near(rgb[0], 0.5 * 1.1 + 0.6) && Near(rgb[1], 0.4 * 1.1 + 0.3) && Near(rgb[2], 0.3 * 1.1);
            Check(away && half && toward,
                  "N.L < 0: no direct light and the wrap under 0.9 (0.7 x H at N.L -1, 0.8 x H at -0.5); N.L = 1: 1.1 x H + Direct -- the " +
                  "wrap 0.7 + 0.4 (0.5 + 0.5 N.L) NOT saturated");

            // Unlit: lighting mode 0 returns the base, whatever the light.
            var albedo = new[] { 0.7, 0.3, 0.2 };
            ModelOutdoorLight.Unlit(albedo, rgb);
            Check(rgb[0] == 0.7 && rgb[1] == 0.3 && rgb[2] == 0.2, "unlit (M2 0x01 and additive, world-model F_UNLIT): the base as it is (lighting mode 0)");

            // Shadow 0 removes D only: the hemisphere keeps A; the flat class keeps sat(Ambient) (the M2 and world-model placement,
            // 2977063 p_203:199-212, 5446718 p_17:164-176), not the terrain flat class's 0.7 x sat(Ambient + D).
            var l = new[] { 0.0, 0.8, 0.6 };
            var lit = new double[3];
            var shadowed = new double[3];
            var f = new double[3];
            ModelOutdoorLight.Shade(albedo, up, l, ambient, horizon, ground, direct, false, 1.0, 1.0, 0.0, lit);
            ModelOutdoorLight.Shade(albedo, up, l, ambient, horizon, ground, direct, false, 0.0, 1.0, 0.0, shadowed);
            double wrapUp = 0.7 + 0.4 * (0.5 + 0.5 * 0.8);
            bool hemiShadow = true;
            for (int c = 0; c < 3; c++)
                hemiShadow &= Near(shadowed[c], albedo[c] * ambient[c] * wrapUp) && Near(lit[c] - shadowed[c], albedo[c] * direct[c] * 0.8);
            ModelOutdoorLight.Shade(albedo, up, l, ambient, horizon, ground, direct, true, 0.0, 1.0, 0.0, shadowed);
            ModelOutdoorLight.Factor(up, l, ambient, horizon, ground, direct, true, 0.0, 1.0, f);
            bool flatShadow = true;
            for (int c = 0; c < 3; c++)
                flatShadow &= Near(shadowed[c], albedo[c] * ambient[c]) && Near(f[c], ambient[c]) &&
                              Math.Abs(shadowed[c] - 0.7 * albedo[c] * Math.Min(1.0, ambient[c] + direct[c] * 0.8)) > 1e-3;
            Check(hemiShadow && flatShadow,
                  "shadow 0 removes the direct term only: the hemisphere keeps base x A, the flat class keeps base x sat(Ambient) -- the M2 and " +
                  "world-model placement (2977063 p_203:199-212, 5446718 p_17:164-176), not the terrain flat class's 0.7 + 0.3 x shadow on the " +
                  "whole colour");

            // AO scales A only; the local light enters the sqrt combine (hemisphere) and the saturate (flat class).
            var aoHalf = new double[3];
            ModelOutdoorLight.Shade(albedo, up, l, ambient, horizon, ground, direct, false, 1.0, 0.5, 0.0, aoHalf);
            bool ao = true;
            for (int c = 0; c < 3; c++)
                ao &= Near(aoHalf[c], albedo[c] * (0.5 * ambient[c] * wrapUp + direct[c] * 0.8));
            var local = new double[3];
            ModelOutdoorLight.Shade(albedo, up, l, ambient, horizon, ground, direct, false, 1.0, 1.0, 0.25, local);
            bool sqrtCombine = true;
            for (int c = 0; c < 3; c++)
                sqrtCombine &= Near(local[c], Math.Sqrt(lit[c] * lit[c] + albedo[c] * albedo[c] * 0.25));
            ModelOutdoorLight.Shade(albedo, up, l, ambient, horizon, ground, direct, true, 1.0, 1.0, 0.25, local);
            bool flatLocal = Near(local[0], albedo[0] * 1.0) && Near(local[2], albedo[2] * Math.Min(1.0, 0.25 + 0.3 + 0.0));
            Check(ao && sqrtCombine && flatLocal,
                  "AO scales A only (D kept); the local light enters as sqrt((base F)^2 + base^2 local) in the hemisphere class (M2 p_1:278-279) " +
                  "and inside the saturate in the flat class (p_195); both held 0 in Stage 10");
        }

        // ================================================================================== 5. the world model

        static void WorldModelTests()
        {
            var r = new Random(7);
            var m2 = new double[3];
            var wmo = new double[3];
            double worst = 0.0, worstFlat = 0.0;
            double[] gate = ModelOutdoorLight.WmoGateOffBase;
            for (int k = 0; k < 2000; k++)
            {
                double[] n = RandomUnit(r), l = RandomUnit(r);
                double s = 0.6 + 0.4 * r.NextDouble();               // raw lengths below 1 too
                n = new[] { n[0] * s, n[1] * s, n[2] * s };
                double[] albedo = RandomColour(r, false);
                double[] ambient = RandomColour(r, false), horizon = RandomColour(r, true), ground = RandomColour(r, true), direct = RandomColour(r, false);
                ModelOutdoorLight.ShadeM2(albedo, n, l, ambient, horizon, ground, direct, false, m2);
                ModelOutdoorLight.ShadeWmo(albedo, n, l, ambient, horizon, ground, direct, false, wmo);
                for (int c = 0; c < 3; c++)
                    worst = Math.Max(worst, Math.Abs(wmo[c] - m2[c] * gate[c]));
                ModelOutdoorLight.ShadeM2(albedo, n, l, ambient, horizon, ground, direct, true, m2);
                ModelOutdoorLight.ShadeWmo(albedo, n, l, ambient, horizon, ground, direct, true, wmo);
                for (int c = 0; c < 3; c++)
                    worstFlat = Math.Max(worstFlat, Math.Abs(wmo[c] - m2[c] * gate[c]));
            }
            Check(gate[0] == 1.0 && gate[2] == 1.0 && Near(gate[1], 0.99998) && ModelOutdoorLight.WmoGateOffGreen == 2.0 * 0.49999 &&
                  worst <= 1e-12 && worstFlat <= 1e-12,
                  "world model: the base albedo x (1, 0.99998, 1) -- the pixel program's 2 x the vertex programs' gate-off COLOR0 (0.5, 0.49999, " +
                  "0.5) -- so ShadeWmo = ShadeM2 x (1, 0.99998, 1) per channel with local 0, hemisphere and flat arm alike (2,000 random cases, " +
                  "raw normals 0.6-1 long; worst " + worst.ToString("E1", CultureInfo.InvariantCulture) + " / " +
                  worstFlat.ToString("E1", CultureInfo.InvariantCulture) + ")");
            // The world model's flat arm by hand: albedo x (1, 0.99998, 1) x sat(Ambient + D), E 0, K 0.
            var albedo2 = new[] { 0.8, 0.6, 0.4 };
            ModelOutdoorLight.ShadeWmo(albedo2, new[] { 0.0, 1.0, 0.0 }, new[] { 0.0, 1.0, 0.0 }, new[] { 0.5, 0.4, 0.3 }, new[] { 0.2, 0.2, 0.2 },
                                       new[] { 0.1, 0.0, 0.0 }, new[] { 0.6, 0.3, 0.0 }, true, wmo);
            Check(Near(wmo[0], 0.8 * 1.0) && Near(wmo[1], 0.6 * 0.99998 * 0.7) && Near(wmo[2], 0.4 * 0.3),
                  "world model, flat arm: albedo x (1, 0.99998, 1) x sat(Ambient + D) (red saturated at 1.1 -> 1)");
        }

        // ================================================================================== 6. the contexts

        static byte[] ModdRecord(uint nameIndex, float x, float y, float z)
        {
            var r = new byte[40];
            BitConverter.GetBytes(nameIndex).CopyTo(r, 0);
            BitConverter.GetBytes(x).CopyTo(r, 4);
            BitConverter.GetBytes(y).CopyTo(r, 8);
            BitConverter.GetBytes(z).CopyTo(r, 12);
            BitConverter.GetBytes(1f).CopyTo(r, 28);                 // quaternion (0, 0, 0, 1)
            BitConverter.GetBytes(1f).CopyTo(r, 32);                 // scale 1
            return r;
        }

        static void ContextRuleTests()
        {
            bool nob;
            bool ext = WorldLightContexts.Group(WmoGroupFlags.Outdoor, out nob) == WorldLightContext.Outdoor && !nob;
            bool intr = WorldLightContexts.Group(WmoGroupFlags.Indoor, out nob) == WorldLightContext.InteriorInterim && !nob;
            bool both = WorldLightContexts.Group(WmoGroupFlags.Outdoor | WmoGroupFlags.Indoor, out nob) == WorldLightContext.InteriorInterim && nob;
            bool neither = WorldLightContexts.Group(0x800, out nob) == WorldLightContext.InteriorInterim && nob;
            bool lit40 = WorldLightContexts.Group(WmoGroupFlags.Outdoor | WorldLightContexts.ExteriorLitFlag) == WorldLightContext.Outdoor &&
                         WorldLightContexts.Group(WmoGroupFlags.Indoor | WorldLightContexts.ExteriorLitFlag) == WorldLightContext.InteriorInterim;
            Check(ext && intr && both && neither && lit40 && default(WorldLightContext) == WorldLightContext.InteriorInterim,
                  "group context from the MOGP flags: 0x8 without 0x2000 OUTDOOR; 0x2000 INTERIOR_INTERIM; neither or both INTERIOR_INTERIM, " +
                  "said; 0x40 changes nothing; the enum's default is INTERIOR_INTERIM (today's look)");

            // MDDF doodads are outdoor; a WMO doodad keeps the default until the session sets it from its world model.
            List<WorldDoodad> mddf = DoodadPlan.FromMddf(new[]
            {
                new DoodadPlacement { NameId = 5000, UniqueId = 1, ScaleRaw = 1024, Flags = 0x40 },
                new DoodadPlacement { NameId = 0, UniqueId = 2, ScaleRaw = 1024, Flags = 0x40 },
            }, PlacementFrame.Tile, null);
            var rec = new WmoPlacement { NameId = 892927, UniqueId = 55, Flags = 0xC, ScaleRaw = 1024 };
            WorldModelPlacement wmoP = WorldModelPlan.Build(new[] { rec }, PlacementFrame.Tile, null)[0];
            var root0 = new WmoRoot
            {
                DoodadSets = new[] { new WmoDoodadSet { Name = "Set_$DefaultGlobal", StartIndex = 0, Count = 2 } },
                DoodadFileDataIDs = new uint[] { 189678 },
                DoodadDefs = new[] { new WmoDoodadDef { Rotation = WowQuat.Identity, Scale = 1f }, new WmoDoodadDef { Rotation = WowQuat.Identity, Scale = 1f } },
            };
            List<WorldDoodad> fromWmo = DoodadPlan.FromWmo(wmoP, 0, root0, DoodadPlan.SelectSets(root0, 55, 892927, 0));
            Check(mddf.Count == 2 && mddf.TrueForAll(d => d.Context == WorldLightContext.Outdoor) && fromWmo.Count == 2 &&
                  fromWmo.TrueForAll(d => d.Context == WorldLightContext.InteriorInterim) && new WorldDoodad().Context == WorldLightContext.InteriorInterim,
                  "doodads: every MDDF doodad OUTDOOR (FromMddf; the unsupported one too: it is never placed); a WMO doodad INTERIOR_INTERIM " +
                  "until its world model's contexts set it; a new WorldDoodad INTERIOR_INTERIM");
        }

        // The synthetic world model: five groups -- 0 exterior, 1 interior, 2 both flags, 3 neither, 4 interior with 0x40 --
        // and six MODD records: 0 listed by group 0 only, 1 by group 1 only, 2 by groups 0 and 1, 3 by none, 4 by group 2 (both
        // flags: an interior listing), 5 by groups 0 and 4. Groups 0 and 1 draw MOMT entry 0 (shared), the rest entry 1.
        static readonly uint[] ContextFlags =
        {
            WmoGroupFlags.Outdoor, WmoGroupFlags.Indoor, WmoGroupFlags.Outdoor | WmoGroupFlags.Indoor, 0u,
            WmoGroupFlags.Indoor | WorldLightContexts.ExteriorLitFlag,
        };
        static readonly ushort[][] ContextModr = { new ushort[] { 0, 2, 5 }, new ushort[] { 1, 2 }, new ushort[] { 4 }, null, new ushort[] { 5 } };
        static readonly int[] ContextMaterial = { 0, 0, 1, 1, 1 };

        static void ContextModelTests()
        {
            var modd = new byte[6][];
            for (int k = 0; k < 6; k++)
                modd[k] = ModdRecord(0, 2f + k, 2f, 1f);
            WmoRoot root = WmoParser.ParseRoot(WmoSynthetic.ContextRoot(ContextFlags, 7000, 7100, modd), "context root");
            var groups = new WmoGroup[ContextFlags.Length];
            for (int i = 0; i < groups.Length; i++)
                groups[i] = WmoParser.ParseGroup(WmoSynthetic.ContextGroup(i, ContextFlags[i], ContextMaterial[i], ContextModr[i]), "context group " + i, i);
            bool built = root.GroupInfos.Length == 5 && root.Materials.Length == 2 && root.DoodadDefs.Length == 6 &&
                         root.Lod0GroupFileDataIDs.SequenceEqual(new uint[] { 7000, 7001, 7002, 7003, 7004 });
            for (int i = 0; i < groups.Length; i++)
                built &= root.GroupInfos[i].Flags == ContextFlags[i] &&
                         groups[i].Header.Flags == (ContextFlags[i] | (ContextModr[i] != null ? WmoGroupFlags.DoodadRefs : 0u)) &&
                         groups[i].Batches.Length == 1 && groups[i].Batches[0].MaterialId == ContextMaterial[i] &&
                         groups[i].Header.BoundsMax.Z == 10f && root.GroupInfos[i].BoundsMax.Z == 10f;
            Check(built && SquaresStillTheSame(),
                  "WmoSynthetic.ContextRoot / ContextGroup: each group's flags in MOGI and MOGP (0x800 with MODR), one batch on the chosen MOMT " +
                  "entry (groups 0 and 1, exterior and interior, share entry 0), boxes 10 units high, MODD written; SquaresRoot / SquaresGroup " +
                  "unchanged");

            ModdContexts c = WorldLightContexts.Modd(root, groups);
            bool groupsOk = c.Groups.SequenceEqual(new[] { WorldLightContext.Outdoor, WorldLightContext.InteriorInterim, WorldLightContext.InteriorInterim,
                                                           WorldLightContext.InteriorInterim, WorldLightContext.InteriorInterim }) &&
                            c.ExteriorGroups == 1 && c.InteriorGroups == 4 && c.NeitherOrBothGroups == 2 && c.InteriorExteriorLitGroups == 1 &&
                            c.MissingGroups == 0 && !c.Incomplete;
            bool records = c.Records.SequenceEqual(new[] { WorldLightContext.Outdoor, WorldLightContext.InteriorInterim, WorldLightContext.InteriorInterim,
                                                           WorldLightContext.InteriorInterim, WorldLightContext.InteriorInterim, WorldLightContext.InteriorInterim }) &&
                           c.Listing.SequenceEqual(new byte[] { 1, 2, 3, 0, 2, 3 }) && c.Of(0) == WorldLightContext.Outdoor &&
                           c.Of(-1) == WorldLightContext.InteriorInterim && c.Of(6) == WorldLightContext.InteriorInterim;
            bool census = c.Outdoor == 1 && c.InteriorInterim == 5 && c.ListedByExteriorOnly == 1 && c.ListedByInteriorOnly == 2 &&
                          c.ListedByBoth == 2 && c.ListedByNone == 1 && c.BothIndices.SequenceEqual(new[] { 2, 5 }) && c.IncompleteRecords == 0;
            Check(groupsOk && records && census,
                  "contexts: groups exterior / interior / both flags / neither / interior with 0x40 -> 1 exterior, 4 interior (2 with neither or " +
                  "both flags, 1 with 0x40); MODD 0 (exterior only) OUTDOOR; 1 (interior only), 2 (both kinds), 3 (no group), 4 (the both-flags " +
                  "group), 5 (exterior and the 0x40 interior group) INTERIOR_INTERIM; census " + c);

            // Group 1 not read: its MODR is unknown, so every record is interim, the listing of the read groups still kept.
            var partial = (WmoGroup[])groups.Clone();
            partial[1] = null;
            ModdContexts p = WorldLightContexts.Modd(root, partial);
            Check(p.Incomplete && p.MissingGroups == 1 && p.IncompleteRecords == 6 && p.Outdoor == 0 && p.InteriorInterim == 6 &&
                  p.Records.All(x => x == WorldLightContext.InteriorInterim) && p.Listing[0] == ModdContexts.ByExterior &&
                  p.Listing[2] == ModdContexts.ByExterior && p.Listing[1] == 0 && p.Groups[1] == WorldLightContext.InteriorInterim,
                  "an incomplete world model (group 1 could not be read): every MODD record INTERIOR_INTERIM, counted (" + p.IncompleteRecords +
                  "), though record 0 and 2 are now listed by exterior groups only");
            ModdContexts none = WorldLightContexts.Modd(null, null);
            ModdContexts noGroups = WorldLightContexts.Modd(root, new WmoGroup[0]);
            Check(none.Records.Length == 0 && none.Groups.Length == 0 && noGroups.Records.Length == 6 && noGroups.ListedByNone == 6 &&
                  noGroups.Outdoor == 0 && !noGroups.Incomplete,
                  "contexts: no root, nothing; a root without groups lists nothing, every record INTERIOR_INTERIM");
        }

        static bool SquaresStillTheSame()
        {
            WmoRoot r = WmoParser.ParseRoot(WmoSynthetic.SquaresRoot(2, 9000, 9100), "squares");
            WmoGroup g0 = WmoParser.ParseGroup(WmoSynthetic.SquaresGroup(0), "square 0", 0);
            WmoGroup g1 = WmoParser.ParseGroup(WmoSynthetic.SquaresGroup(1, new ushort[] { 0 }), "square 1", 1);
            return r.GroupInfos.Length == 2 && r.GroupInfos[0].Flags == WmoGroupFlags.Outdoor && r.GroupInfos[1].BoundsMax.Z == 0f &&
                   g0.Header.Flags == WmoGroupFlags.Outdoor && g1.Header.Flags == (WmoGroupFlags.Outdoor | 0x800) && g1.Batches[0].MaterialId == 1 &&
                   g0.Header.BoundsMax.Z == 0f;
        }

        static void MddfInsideTests()
        {
            WmoRoot root = WmoParser.ParseRoot(WmoSynthetic.ContextRoot(new[] { WmoGroupFlags.Outdoor, WmoGroupFlags.Indoor }, 7200, 7300, null), "two rooms");
            var groups = new[]
            {
                WmoParser.ParseGroup(WmoSynthetic.ContextGroup(0, WmoGroupFlags.Outdoor, 0), "exterior", 0),
                WmoParser.ParseGroup(WmoSynthetic.ContextGroup(1, WmoGroupFlags.Indoor, 0), "interior", 1),
            };
            // A turned, tilted and scaled placement (a0 20, a1 35, a2 10 deg; scale 2); its extents the root box placed.
            var rec = new WmoPlacement
            {
                NameId = 4242, UniqueId = 77, Position = new WorldFloat3(100f, 20f, 300f), Rotation = new WorldFloat3(20f, 35f, 10f),
                Flags = 0xC, ScaleRaw = 2048,
            };
            WorldModelPlacement p = WorldModelPlan.Build(new[] { rec }, PlacementFrame.Tile, null)[0];
            p.PlaceBox(root.Header.BoundsMin, root.Header.BoundsMax, out p.ExtentsMin, out p.ExtentsMax);
            var models = new List<WorldModelGeometry> { new WorldModelGeometry(p, root, groups) };
            Func<uint, WorldPoint, DoodadSource, string, WorldDoodad> doodad = (fid, w, src, unsupported) =>
                new WorldDoodad { Source = src, FileDataID = fid, UniqueId = fid + 1000, Position = w, Rotation = new[] { 1.0, 0, 0, 0, 1, 0, 0, 0, 1 },
                                  Unsupported = unsupported, Context = src == DoodadSource.Mddf ? WorldLightContext.Outdoor : WorldLightContext.InteriorInterim };
            var list = new List<WorldDoodad>
            {
                doodad(1, p.ToWorld(17, 5, 5), DoodadSource.Mddf, null),          // inside the interior group's box: counted
                doodad(2, p.ToWorld(5, 5, 5), DoodadSource.Mddf, null),           // inside the exterior group only
                doodad(3, p.ToWorld(11, 5, 5), DoodadSource.Mddf, null),          // inside the extents, between the two groups
                doodad(4, p.ToWorld(17, 5, 40), DoodadSource.Mddf, null),         // above the world model: outside its extents
                doodad(5, new WorldPoint(p.Position.X + 17, p.Position.Y + 5, p.Position.Z + 5), DoodadSource.Mddf, null),   // (17, 5, 5) unturned, unscaled
                doodad(6, p.ToWorld(18, 6, 6), DoodadSource.WmoSet, null),        // a WMO doodad: not an MDDF one
                doodad(7, p.ToWorld(18, 6, 6), DoodadSource.Mddf, "the record names FileDataID 0"),   // never placed
                doodad(8, p.ToWorld(21.5, 9.5, 9.5), DoodadSource.Mddf, null),    // inside, near the box's far corner
            };
            InteriorDoodadCensus census = WorldLightContexts.MddfInsideInterior(list, models);
            WorldPoint q = p.ToModel(list[4].Position.X, list[4].Position.Y, list[4].Position.Z);
            bool unturnedOutside = !(q.X >= 12 && q.X <= 22 && q.Y >= 0 && q.Y <= 10 && q.Z >= 0 && q.Z <= 10);
            InteriorDoodadCensus capped = WorldLightContexts.MddfInsideInterior(list, models, 1);
            InteriorDoodadCensus noModels = WorldLightContexts.MddfInsideInterior(list, null);
            Check(census.Doodads == 6 && census.Inside == 2 && census.Names.Count == 2 && census.Names[0].Contains("M2 1 ") &&
                  census.Names[0].Contains("group 1") && census.Names[1].Contains("M2 8 ") && unturnedOutside && capped.Inside == 2 &&
                  capped.Names.Count == 1 && noModels.Doodads == 6 && noModels.Inside == 0,
                  "MDDF doodads inside an interior group's box, under a turned, tilted and scaled placement: the origin taken into the model's " +
                  "frame by the inverse transform after the extents test -- 2 of the 6 placeable MDDF doodads (the WMO doodad and the " +
                  "unsupported one not looked at; the exterior room, the gap between the rooms, above the extents and the same offset unturned " +
                  "and unscaled not counted); the names capped (" + census + ")");
        }

        // ================================================================================== 7. the client's files (WMV_WORLD_AUDIT_CORPUS)

        static readonly string[] Clients = { "wow", "wow_classic_era", "wow_classic", "wow_classic_beta" };

        sealed class Totals
        {
            public int Roots, Complete, Groups, Exterior, Interior, NeitherOrBoth, InteriorLit40, Missing, Records, Outdoor, ExteriorOnly, InteriorOnly,
                       Both, None, IncompleteRoots, IncompleteRecords;

            public void Add(ModdContexts c)
            {
                Roots++;
                if (!c.Incomplete) Complete++;
                Groups += c.ExteriorGroups + c.InteriorGroups;
                Exterior += c.ExteriorGroups;
                Interior += c.InteriorGroups;
                NeitherOrBoth += c.NeitherOrBothGroups;
                InteriorLit40 += c.InteriorExteriorLitGroups;
                Missing += c.MissingGroups;
                Records += c.Records.Length;
                Outdoor += c.Outdoor;
                ExteriorOnly += c.ListedByExteriorOnly;
                InteriorOnly += c.ListedByInteriorOnly;
                Both += c.ListedByBoth;
                None += c.ListedByNone;
                if (c.Incomplete) { IncompleteRoots++; IncompleteRecords += c.IncompleteRecords; }
            }

            public override string ToString()
            {
                return string.Format(CultureInfo.InvariantCulture,
                                     "{0} roots ({1} complete, {2} incomplete: {3} MODD records interim, {4} groups not read); {5} LOD0 groups read: " +
                                     "{6} exterior, {7} interior ({8} with neither or both flags, {9} interior with 0x40); {10} MODD records: listed by " +
                                     "exterior groups only {11} ({12} OUTDOOR), interior only {13}, both kinds {14}, none {15}",
                                     Roots, Complete, IncompleteRoots, IncompleteRecords, Missing, Groups, Exterior, Interior, NeitherOrBoth, InteriorLit40,
                                     Records, ExteriorOnly, Outdoor, InteriorOnly, Both, None);
            }
        }

        static void World07Census(string corpus)
        {
            string folder = Path.Combine(corpus, "world07");
            if (!File.Exists(Path.Combine(folder, "manifest.json")))
            {
                log.Add("  SKIPPED: " + folder + "/manifest.json is missing (python -I Tools/world-audit/world07_corpus.py)");
                return;
            }
            Dictionary<string, WorldModelCorpusTests.ClientFiles> clients = WorldModelCorpusTests.LoadManifest(folder);
            foreach (string name in Clients)
            {
                WorldModelCorpusTests.ClientFiles cf;
                if (!clients.TryGetValue(name, out cf))
                {
                    Info(name + ": not in the corpus manifest");
                    continue;
                }
                var t = new Totals();
                var roots = cf.Role.Where(kv => kv.Value == "wmo root").Select(kv => kv.Key).OrderBy(k => k).ToList();
                foreach (uint id in roots)
                {
                    WmoRoot root = WmoParser.ParseRoot(cf.Read(id), "root " + id);
                    uint[] ids = root.Lod0GroupFileDataIDs;
                    var groups = new WmoGroup[ids.Length];
                    for (int i = 0; i < ids.Length; i++)
                        if (ids[i] != 0 && cf.Has(ids[i]))
                            groups[i] = WmoParser.ParseGroup(cf.Read(ids[i]), "group " + ids[i], i);
                    t.Add(WorldLightContexts.Modd(root, groups));
                }
                Check(t.Roots == roots.Count && t.ExteriorOnly + t.InteriorOnly + t.Both + t.None == t.Records && t.Exterior + t.Interior == t.Groups,
                      "census " + name + " " + cf.Build + " (world07 corpus, the world models of map 0's tiles 30_47 and 31_48): " + t);
            }
        }

        static string Sha256Hex(byte[] b)
        {
            using (SHA256 s = SHA256.Create())
                return Convert.ToHexString(s.ComputeHash(b)).ToLowerInvariant();
        }

        static string FindPins()
        {
            string env = Environment.GetEnvironmentVariable("WMV_WORLD_AUDIT_PINS");
            if (!string.IsNullOrEmpty(env) && File.Exists(env))
                return env;
            foreach (string start in new[] { Environment.CurrentDirectory, AppContext.BaseDirectory })
                for (var d = new DirectoryInfo(start); d != null; d = d.Parent)
                {
                    string p = Path.Combine(d.FullName, "Tools", "world-audit", "tests", "corpus_hashes.json");
                    if (File.Exists(p))
                        return p;
                }
            return null;
        }

        static bool IsChunk(byte[] b, int at, string tag)
        {
            return at + 8 <= b.Length && b[at] == tag[3] && b[at + 1] == tag[2] && b[at + 2] == tag[1] && b[at + 3] == tag[0];
        }

        /// <summary>"root", "group" or null: the chunk after MVER.</summary>
        static string Kind(byte[] b)
        {
            if (b.Length < 16 || !IsChunk(b, 0, "MVER"))
                return null;
            int next = 8 + BitConverter.ToInt32(b, 4);
            return IsChunk(b, next, "MOHD") ? "root" : IsChunk(b, next, "MOGP") ? "group" : null;
        }

        static void StormwindWindow(string corpus)
        {
            string dir = Path.Combine(corpus, "assets", "wow");
            if (!Directory.Exists(dir))
            {
                log.Add("  SKIPPED: " + dir + " is missing (the audit's asset window)");
                return;
            }
            // The file set first: the digest corpus_hashes.json pins for "assets/wow" (WorldCorpusValidation's rule).
            string pins = FindPins();
            var names = Directory.GetFiles(dir, "*.bin").Select(Path.GetFileName).OrderBy(n => n, StringComparer.Ordinal).ToList();
            var bytes = new Dictionary<uint, byte[]>();
            var sb = new StringBuilder();
            foreach (string n in names)
            {
                byte[] b = File.ReadAllBytes(Path.Combine(dir, n));
                sb.Append(n).Append(' ').Append(Sha256Hex(b)).Append('\n');
                uint id;
                if (uint.TryParse(Path.GetFileNameWithoutExtension(n), NumberStyles.None, CultureInfo.InvariantCulture, out id))
                    bytes[id] = b;
            }
            string digest = Sha256Hex(Encoding.ASCII.GetBytes(sb.ToString()));
            string pinned = null;
            int pinnedFiles = -1;
            if (pins != null)
                using (JsonDocument doc = JsonDocument.Parse(File.ReadAllText(pins)))
                {
                    JsonElement set;
                    if (doc.RootElement.GetProperty("sets").TryGetProperty("assets/wow", out set))
                    {
                        pinned = set.GetProperty("digest").GetString();
                        pinnedFiles = set.GetProperty("files").GetInt32();
                    }
                }
            bool pinnedOk = pinned != null && digest == pinned && names.Count == pinnedFiles;
            Check(pinnedOk, "the Retail asset window <corpus>/assets/wow: " + names.Count + " files, digest " + digest.Substring(0, 16) +
                            (pinnedOk ? " -- as Tools/world-audit/tests/corpus_hashes.json pins it" : " -- NOT as pinned (" + (pins ?? "no pins file") + ")"));
            if (!pinnedOk)
                return;

            var roots = new List<uint>();
            var groupFiles = new HashSet<uint>();
            foreach (KeyValuePair<uint, byte[]> kv in bytes)
            {
                string k = Kind(kv.Value);
                if (k == "root") roots.Add(kv.Key);
                else if (k == "group") groupFiles.Add(kv.Key);
            }
            roots.Sort();
            var t = new Totals();
            int incompleteSkipped = 0;
            foreach (uint id in roots)
            {
                WmoRoot root = WmoParser.ParseRoot(bytes[id], "root " + id);
                uint[] ids = root.Lod0GroupFileDataIDs;
                if (ids.Length == 0 || !ids.All(groupFiles.Contains))
                {
                    incompleteSkipped++;
                    continue;
                }
                var groups = new WmoGroup[ids.Length];
                for (int i = 0; i < ids.Length; i++)
                    groups[i] = WmoParser.ParseGroup(bytes[ids[i]], "group " + ids[i], i);
                t.Add(WorldLightContexts.Modd(root, groups));
            }
            int listed = t.ExteriorOnly + t.InteriorOnly + t.Both;
            Check(roots.Count == 109 && t.Roots == 52 && t.Groups == 531 && t.Exterior == 300 && t.Interior == 231 && t.NeitherOrBoth == 0 &&
                  listed == 11470 && t.Both == 6 && t.IncompleteRoots == 0,
                  "the Retail Stormwind window against the plan's 2.2: " + roots.Count + " roots, " + t.Roots + " with every LOD0 group present (" +
                  incompleteSkipped + " others not counted); " + t.Groups + " groups, " + t.Exterior + " exterior / " + t.Interior +
                  " interior, exactly one of 0x8 and 0x2000 on every one (" + t.NeitherOrBoth + " with neither or both); " + listed +
                  " MODD records listed, " + t.Both + " by both kinds (plan: 52, 531, 300 / 231, 11,470, 6)");
            Info("the Retail window, the whole census: " + t);
        }

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();
            Action[] steps = { FixtureTests, EquivalenceTests, RawNormalTests, RuleTests, WorldModelTests, ContextRuleTests, ContextModelTests, MddfInsideTests };
            string[] titles =
            {
                "Model light: the client's own programs at the Southfury light (2977063 p_1, p_195)",
                "Model light: the terrain's light with a unit normal, and Factor",
                "Model light: the raw interpolated normal",
                "Model light: the rules (zero colours, N.L < 0, unlit, shadow on D only, AO, local)",
                "Model light: the world model's base and flat arm",
                "Contexts: the group and doodad rules",
                "Contexts: a synthetic world model (WmoSynthetic.ContextRoot / ContextGroup)",
                "Contexts: MDDF doodads inside an interior",
            };
            for (int k = 0; k < steps.Length; k++)
            {
                log.Add(titles[k]);
                try { steps[k](); }
                catch (Exception e) { Check(false, steps[k].Method.Name + " threw " + e.GetType().Name + ": " + e.Message); }
            }
            string corpus = Environment.GetEnvironmentVariable("WMV_WORLD_AUDIT_CORPUS");
            log.Add("Contexts: the census of the client's world models (world07 corpus, the Retail asset window)");
            if (string.IsNullOrEmpty(corpus))
                log.Add("  SKIPPED: WMV_WORLD_AUDIT_CORPUS is not set (the audit data is extracted locally, not committed)");
            else
            {
                try { World07Census(corpus); }
                catch (Exception e) { Check(false, "World07Census threw " + e.GetType().Name + ": " + e.Message); }
                try { StormwindWindow(corpus); }
                catch (Exception e) { Check(false, "StormwindWindow threw " + e.GetType().Name + ": " + e.Message); }
            }
            log.Add(string.Format("WorldModelLightTests: {0} failure(s)", failures));
            if (output != null)
                foreach (string line in log)
                    output(line);
            return failures;
        }
    }
}
