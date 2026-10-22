// SkyTerrainLightTests.cs
//
// Unit tests of Stage 10 steps 4 and 5 (docs/world-viewer/10-light-sky-fog.md 4.3, 4.5, 6.8, 6.10): the sky's colour dome
// (Assets/Scripts/Wow/World/SkyDome.cs: the construction against the public table A the spec lists and table B, the counts,
// the triangulation, each ring's elevation and horizon ramp, the day curve, the inputs -- the end-fog lean and the zero-colour
// rules --, the sky program and the CPU twin the self-test compares the GPU with) and the terrain light
// (Assets/Scripts/Wow/World/TerrainLight.cs: the MCNR decode on synthetic bytes and through the parser and mesh builder --
// the bytes kept only when the build asks, and counted --, the fallback, the client's hemisphere and flat formulas with the
// normal used at its own length).
//
// With WMV_MCNR_TILES naming a folder holding the four Southfury root ADTs of Retail 12.1.5 (Kalimdor 39_29, 40_29, 39_30,
// 40_30: <FileDataID>.bin, each pinned below by its SHA-256) it also PROVES the MCNR mapping again: the decode against the
// tiles' geometry normals (TerrainMeshBuilder's, away from the one-sided tile edges) -- mean under 3 deg, the best of the 48
// axis orders and signs by a wide margin; it measures the decoded lengths the light uses; and it checks the four tile seams
// (the two tiles' MCNR at each shared vertex within 1 deg: the data's continuity and the vertex match, not the decode).
// Without it that part reports SKIPPED.
// Framework-free like the other suites: RunAll returns the failure count.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class SkyTerrainLightTests
    {
        static int failures;
        static readonly List<string> log = new List<string>();

        static void Check(bool condition, string name)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name); failures++; }
        }

        static bool Near(double a, double b, double eps = 1e-9) { return Math.Abs(a - b) <= eps; }

        static string F(double v, string fmt = "0.####") { return v.ToString(fmt, CultureInfo.InvariantCulture); }

        // ================================================================================== the dome

        // The spec's table A (sky_terrain_spec.md step 4), rounded there to 5 decimals (ring 4's height to 6): radius, height.
        static readonly double[] SpecR = { 0.0, 0.49035, 0.56800, 0.64133, 0.65309, 0.6875, 0.0 };
        static readonly double[] SpecZ = { 0.29289, 0.13915, 0.08489, 0.02416, 0.013455, -0.005, -1.70711 };

        static void DomeTests()
        {
            var r = new double[SkyDome.ProfilePoints];
            var z = new double[SkyDome.ProfilePoints];
            SkyDome.Profile(false, r, z);
            double worst = 0.0;
            for (int k = 0; k < SkyDome.ProfilePoints; k++)
                worst = Math.Max(worst, Math.Max(Math.Abs(r[k] - SpecR[k]), Math.Abs(z[k] - SpecZ[k])));
            var e = new double[SkyDome.ProfilePoints];
            for (int k = 0; k < SkyDome.ProfilePoints; k++)
                e[k] = SkyDome.ElevationDeg(r[k], z[k]);
            Check(worst <= 5.1e-6 && Near(e[0], 90) && Near(e[1], 15.84, 0.005) && Near(e[2], 8.50, 0.005) && Near(e[3], 2.16, 0.005) &&
                  Near(e[4], 1.18, 0.005) && Near(e[5], -0.42, 0.005) && Near(e[6], -90),
                  "dome table A: the cubic construction at latitudes 0, 0.17, 0.20, 0.23, 0.235, 0.25, 1 (ring 5's height -0.005) equals the spec's " +
                  "table within its rounding (worst " + F(worst, "0.#######") + "); elevations " + string.Join(" / ", e.Select(x => F(x, "0.00")).ToArray()) +
                  " deg (15.84, 8.50, 2.16, 1.18, -0.42)");
            var rb = new double[SkyDome.ProfilePoints];
            var zb = new double[SkyDome.ProfilePoints];
            SkyDome.Profile(true, rb, zb);
            bool sharedAB = true;
            foreach (int k in new[] { 0, 1, 2, 3, 6 })
                sharedAB &= rb[k] == r[k] && zb[k] == z[k];
            // The report lists table B's ring 4 as (0.66472, 0.002586): the construction with the exact cos(pi/4) gives (0.664704, 0.0025892),
            // within 2e-5 -- the older table's own rounding; 0.22 deg either way (B is a debug arm).
            Check(sharedAB && Near(rb[4], 0.66472, 2e-5) && Near(zb[4], 0.002586, 5e-6) && Near(rb[5], 0.6875, 1e-12) && Near(zb[5], -0.01961, 5e-6) &&
                  Near(SkyDome.ElevationDeg(rb[4], zb[4]), 0.22, 0.005) && Near(SkyDome.ElevationDeg(rb[5], zb[5]), -1.63, 0.005),
                  "dome table B (-wmvSkyTable=b): the same construction with ring 4 at latitude 0.24 and ring 5's own height: (" + F(rb[4], "0.######") + ", " +
                  F(zb[4], "0.#######") + ") and (" + F(rb[5]) + ", " + F(zb[5], "0.#####") + "), 0.22 and -1.63 deg (the report's table within 2e-5); " +
                  "the top pole and rings 1-3 shared");
            Check(Near(Math.Min(1, Math.Max(0, SkyDome.HorizonRamp * z[4])), 0.67, 0.005) && Near(Math.Min(1, Math.Max(0, SkyDome.HorizonRamp * zb[4])), 0.13, 0.005) &&
                  SkyDome.HorizonRamp * z[3] >= 1.0 && SkyDome.HorizonRamp * z[5] <= 0.0,
                  "the horizon ramp sat(50 z) on the raw table height: 1 down to ring 3, 0.67 on ring 4 (table B 0.13), 0 from ring 5");

            var p = new float[3 * SkyDome.VertexCount];
            var s = new float[SkyDome.VertexCount];
            var ix = new int[3 * SkyDome.TriangleCount];
            SkyDome.Build(false, p, s, ix);
            bool rings = true;
            for (int ring = 1; ring <= SkyDome.RingCount; ring++)
                for (int j = 0; j < SkyDome.Segments; j++)
                {
                    int v = 1 + (ring - 1) * SkyDome.Segments + j;
                    double rad = Math.Sqrt(p[3 * v] * p[3 * v] + p[3 * v + 2] * p[3 * v + 2]);
                    double lon = Math.Atan2(p[3 * v + 2], p[3 * v]) * 180.0 / Math.PI;
                    if (lon < -1e-3) lon += 360.0;
                    rings &= Near(rad, r[ring], 1e-6) && p[3 * v + 1] == (float)z[ring] && s[v] == SkyDome.SlotOf[ring] && Near(lon, 15.0 * j, 1e-4);
                }
            bool poles = p[0] == 0f && p[1] == (float)z[0] && p[2] == 0f && s[0] == 0f && p[3 * 121 + 1] == (float)z[6] && s[121] == 5f;
            bool indices = ix.All(i => i >= 0 && i < SkyDome.VertexCount);
            int degenerate = 0;
            var used = new int[SkyDome.VertexCount];
            for (int t = 0; t < SkyDome.TriangleCount; t++)
            {
                int a = ix[3 * t], b = ix[3 * t + 1], c = ix[3 * t + 2];
                if (a == b || b == c || a == c) degenerate++;
                used[a]++; used[b]++; used[c]++;
            }
            Check(SkyDome.VertexCount == 122 && SkyDome.TriangleCount == 240 && rings && poles && indices && degenerate == 0 && used.All(u => u > 0) &&
                  used[0] == 24 && used[121] == 24,
                  "dome mesh: 122 vertices (the top pole, 5 rings of 24 at 15-deg longitude steps, the bottom pole) with their slots, 240 triangles " +
                  "(a 24-triangle fan at each pole, quad strips between the rings), none degenerate, every vertex used");

            // Closed and seen from inside: every direction meets exactly the dome (2,000 directions over the sphere).
            var rnd = new Random(10);
            var inputs = new SkyInputs();
            int missed = 0;
            for (int k = 0; k < 2000; k++)
            {
                double u = 2 * rnd.NextDouble() - 1, phi = 2 * Math.PI * rnd.NextDouble(), q = Math.Sqrt(1 - u * u);
                double rr, gg, bb;
                if (!SkyDome.Trace(p, s, ix, q * Math.Cos(phi), u, q * Math.Sin(phi), inputs, out rr, out gg, out bb))
                    missed++;
            }
            Check(missed == 0, "dome: closed around its centre -- every one of 2,000 directions meets a triangle (" + missed + " missed)");
        }

        // ================================================================================== day curve, inputs, the sky program

        static void SkyProgramTests()
        {
            Check(SkyDome.DayCurve(720) == 0 && SkyDome.DayCurve(780) == 0 && Near(SkyDome.DayCurve(810), 0.5) && SkyDome.DayCurve(840) == 1 &&
                  SkyDome.DayCurve(1440) == 1 && SkyDome.DayCurve(2160) == 1 && Near(SkyDome.DayCurve(2190), 0.5) && SkyDome.DayCurve(2220) == 0 &&
                  SkyDome.DayCurve(2760) == 0 && SkyDome.DayCurve(0) == 0,
                  "day curve (INFERRED (public)): 0 at 06:00 and 06:30, 0.5 at 06:45, 1 from 07:00 through 18:00, 0.5 at 18:15, 0 from 18:30, 0 at night");

            // A state's channels by hand: the six sky colours, EndFog, SunFog, the floats.
            var v = new double[WorldLightSample.Count];
            Action<int, double, double, double> col = (c, r, g, b) =>
            {
                int at = WorldLightSample.ColourAt(c);
                v[at] = r; v[at + 1] = g; v[at + 2] = b;
            };
            col(WorldLightColumns.SkyTopColor, 0.1, 0.2, 0.3);
            col(WorldLightColumns.SkyMiddleColor, 0.2, 0.3, 0.4);
            col(WorldLightColumns.SkyBand1Color, 0.3, 0.4, 0.5);
            col(WorldLightColumns.SkyBand2Color, 0.4, 0.5, 0.6);
            col(WorldLightColumns.SkySmogColor, 0.5, 0.6, 0.7);
            col(WorldLightColumns.SkyFogColor, 0.2, 0.2, 0.2);
            col(WorldLightColumns.EndFogColor, 0.6, 0.4, 0.2);
            col(WorldLightColumns.SunFogColor, 0.9, 0.65, 0.47);
            v[WorldLightSample.FloatAt(WorldLightColumns.EndFogColorDistance)] = 14000.0;
            v[WorldLightSample.FloatAt(WorldLightColumns.SunFogAngle)] = 0.25;
            v[WorldLightSample.FloatAt(WorldLightColumns.SunFogStrength)] = 0.3;
            var s = new SkyInputs();
            SkyDome.Inputs(v, 810, 7000.0, true, s);
            bool half = Near(s.FogLean, 0.5) && Near(s.Colours[15], 0.4) && Near(s.Colours[16], 0.3) && Near(s.Colours[17], 0.2) && Near(s.Colours[0], 0.1) &&
                        Near(s.Colours[14], 0.7) && s.Angle == 0.25 && s.Tint[0] == 0.9 && Near(s.WeightAbove, 0.15) && s.WeightBelow == s.WeightAbove &&
                        Near(s.DayCurve, 0.5);
            v[WorldLightSample.FloatAt(WorldLightColumns.EndFogColorDistance)] = 900.0;
            SkyDome.Inputs(v, 1440, 7000.0, true, s);
            bool full = s.FogLean == 1.0 && Near(s.Colours[15], 0.6) && Near(s.WeightAbove, 0.3);
            v[WorldLightSample.FloatAt(WorldLightColumns.EndFogColorDistance)] = 0.0;
            SkyDome.Inputs(v, 1440, 7000.0, true, s);
            bool zeroDistance = s.FogLean == 1.0 && Near(s.Colours[15], 0.6);
            SkyDome.Inputs(v, 1440, 7000.0, false, s);
            bool switchedOff = s.FogLean == 0.0 && s.Colours[15] == 0.2;
            col(WorldLightColumns.EndFogColor, 0, 0, 0);
            SkyDome.Inputs(v, 1440, 7000.0, true, s);
            bool zeroEndFog = s.FogLean == 0.0 && s.Colours[15] == 0.2;
            col(WorldLightColumns.SunFogColor, 0, 0, 0);
            SkyDome.Inputs(v, 1440, 7000.0, true, s);
            bool zeroTint = s.Angle == 1.0;
            Check(half && full && zeroDistance && switchedOff && zeroEndFog && zeroTint,
                  "sky inputs: ring 5 leans to EndFogColor by sat(far clip / EndFogColorDistance) (7000 / 14000: half-way; 7000 / 900 and a distance " +
                  "of 0: all the way), -wmvSkyEndFog=off and a zero EndFogColor keep SkyFogColor; the tint a = SunFogAngle, C = SunFogColor, both " +
                  "strengths SunFogStrength x the day curve (0.3 x 0.5 at 06:45); a zero SunFogColor turns the tint off (a = 1)");

            // The pixel program.
            var t = new SkyInputs { Angle = 0.5, WeightAbove = 0.8, WeightBelow = 0.2 };
            t.Tint[0] = 1.0; t.Tint[1] = 0.5; t.Tint[2] = 0.0;
            t.Sun[0] = 1.0; t.Sun[1] = 0.0; t.Sun[2] = 0.0;
            double r1, g1, b1;
            SkyDome.Shade(0.2, 0.4, 0.6, 3.0, 0.0, 0.0, 0.05, t, out r1, out g1, out b1);           // at the sun, above the ramp: x 1, w 0.8
            bool atSun = Near(r1, 0.2 + 0.8 * (1.0 - 0.2)) && Near(g1, 0.4 + 0.8 * (0.5 - 0.4)) && Near(b1, 0.6 + 0.8 * (0.0 - 0.6));
            SkyDome.Shade(0.2, 0.4, 0.6, 3.0, 0.0, 0.0, -0.01, t, out r1, out g1, out b1);          // below the ramp: w 0.2
            bool below = Near(r1, 0.2 + 0.2 * 0.8);
            SkyDome.Shade(0.2, 0.4, 0.6, 3.0, 0.0, 0.0, 0.01, t, out r1, out g1, out b1);           // on the ramp: w mix(0.2, 0.8, 0.5)
            bool ramp = Near(r1, 0.2 + 0.5 * 0.8);
            double c75 = 0.75;                                                                         // dot 0.75: x = (0.75 - 0.5) / 0.5 = 0.5
            SkyDome.Shade(0.2, 0.4, 0.6, c75, Math.Sqrt(1 - c75 * c75), 0.0, 1.0, t, out r1, out g1, out b1);
            bool cubic = Near(r1, 0.2 + 0.125 * 0.8 * 0.8);
            SkyDome.Shade(0.2, 0.4, 0.6, 0.4, Math.Sqrt(1 - 0.16), 0.0, 1.0, t, out r1, out g1, out b1);   // dot 0.4 < a: untouched
            bool outside = r1 == 0.2 && g1 == 0.4 && b1 == 0.6;
            t.Angle = 1.0;
            SkyDome.Shade(0.2, 0.4, 0.6, 3.0, 0.0, 0.0, 0.05, t, out r1, out g1, out b1);
            bool off = r1 == 0.2 && g1 == 0.4 && b1 == 0.6;
            Check(atSun && below && ramp && cubic && outside && off,
                  "sky program (PROVEN_BYTES 2977085): v + sat(x^3) mix(wBelow, wAbove, sat(50 z)) (C - v) with x = sat(cos - a) / (1 - a): full " +
                  "at the sun, the below strength under z 0, half-way at z 0.01, x^3 = 0.125 at x 0.5, untouched outside the cone and with a = 1");

            // The CPU twin along a vertex longitude (where every facet edge is a profile segment): the colour is the profile's linear
            // interpolation at the ray's crossing -- the step-1 measurement's own dome evaluation.
            var p = new float[3 * SkyDome.VertexCount];
            var sl = new float[SkyDome.VertexCount];
            var ix = new int[3 * SkyDome.TriangleCount];
            SkyDome.Build(false, p, sl, ix);
            var dome = new SkyInputs();
            for (int k = 0; k < 18; k++)
                dome.Colours[k] = (k * 37 % 17) / 17.0;
            var r = new double[SkyDome.ProfilePoints];
            var z = new double[SkyDome.ProfilePoints];
            SkyDome.Profile(false, r, z);
            for (int k = 0; k < SkyDome.ProfilePoints; k++)
            {
                r[k] = (float)r[k];
                z[k] = (float)z[k];
            }
            double worst = 0.0;
            int hits = 0;
            string sample = "";
            foreach (double elev in new[] { 60.0, 20.0, 12.0, 5.0, 1.5, 0.5, -0.2, -10.0, -60.0 })
            {
                double dx = Math.Cos(elev * Math.PI / 180.0), dy = Math.Sin(elev * Math.PI / 180.0);
                double wr = 0, wg = 0, wb = 0;
                bool found = false;
                for (int k = 0; k + 1 < SkyDome.ProfilePoints; k++)
                {
                    double ex = r[k + 1] - r[k], ez = z[k + 1] - z[k];
                    double den = dx * (-ez) - dy * (-ex);
                    if (Math.Abs(den) < 1e-15) continue;
                    double along = (r[k] * (-ez) - z[k] * (-ex)) / den;
                    double u = Math.Abs(ex) > Math.Abs(ez) ? (along * dx - r[k]) / ex : (along * dy - z[k]) / ez;
                    if (along > 0 && u >= -1e-9 && u <= 1 + 1e-9)
                    {
                        int a = SkyDome.SlotOf[k], b = SkyDome.SlotOf[k + 1];
                        wr = dome.Colours[3 * a] + (dome.Colours[3 * b] - dome.Colours[3 * a]) * u;
                        wg = dome.Colours[3 * a + 1] + (dome.Colours[3 * b + 1] - dome.Colours[3 * a + 1]) * u;
                        wb = dome.Colours[3 * a + 2] + (dome.Colours[3 * b + 2] - dome.Colours[3 * a + 2]) * u;
                        found = true;
                        break;
                    }
                }
                double tr, tg, tb;
                if (SkyDome.Trace(p, sl, ix, dx, dy, 0.0, dome, out tr, out tg, out tb) && found)
                    hits++;
                worst = Math.Max(worst, Math.Max(Math.Abs(tr - wr), Math.Max(Math.Abs(tg - wg), Math.Abs(tb - wb))));
                if (elev == 5.0)
                    sample = "(" + F(tr) + ", " + F(tg) + ", " + F(tb) + ") at 5 deg";
            }
            Check(worst < 1e-6 && hits == 9,
                  "sky CPU twin (SkyDome.Trace): along a vertex longitude it equals the profile's linear interpolation at nine elevations from 60 " +
                  "to -60 deg (" + hits + " met; worst " + F(worst, "0.#########") + "; " + sample + ")");
        }

        // ================================================================================== MCNR and the terrain light

        static void McnrTests()
        {
            var raw = new sbyte[TerrainMcnr.Bytes];
            raw[0] = 10; raw[1] = -20; raw[2] = 100;
            raw[3 * 144] = -127; raw[3 * 144 + 1] = 127; raw[3 * 144 + 2] = 0;
            float x, y, z;
            TerrainMcnr.Unity(raw, 0, out x, out y, out z);
            float ux, uy, uz;
            WorldToUnity.Direction(10 / 127.0, -20 / 127.0, 100 / 127.0, out ux, out uy, out uz);
            bool first = x == (float)(20 / 127.0) && y == (float)(100 / 127.0) && z == (float)(10 / 127.0) && x == ux && y == uy && z == uz;
            TerrainMcnr.Unity(raw, 144, out x, out y, out z);
            bool last = x == -1f && y == 0f && z == -1f;
            Check(first && last,
                  "MCNR decode: the bytes (b0, b1, b2) are the game frame's (X north, Y west, Z up) / 127, so Unity's (-b1, b2, b0) / 127 -- exactly " +
                  "WorldToUnity.Direction of the game vector; vertex 144 read from bytes 432-434");

            // Through the parser and the mesh builder (the synthetic root writes MCNR byte k = k % 127): kept only when the build
            // asks (keepMcnr: the player while the outdoor light's normals may be wanted), and counted apart from the mesh bytes.
            AdtRoot root = AdtParser.ParseRoot(AdtSynthetic.FlatRoot(32, 50), 32, 50);
            TerrainTileMesh plain = TerrainMeshBuilder.Build(root, 32, 50, WorldToUnity.TileOrigin(32, 50));
            TerrainTileMesh tile = TerrainMeshBuilder.Build(root, 32, 50, WorldToUnity.TileOrigin(32, 50), true);
            bool keptOnlyAsked = plain.McnrBytes == 0 && plain.Chunks.All(c => c.McnrRaw == null) && tile.ChunksBuilt == 256 &&
                                 tile.McnrBytes == 256L * TerrainMcnr.Bytes && tile.Chunks.All(c => c.McnrRaw != null) && tile.MeshBytes == plain.MeshBytes;
            Check(keptOnlyAsked,
                  "MCNR kept with the mesh only when the build asks (keepMcnr): none by default (the tile holds nothing more than before), " +
                  "256 x 435 = " + tile.McnrBytes + " bytes counted in McnrBytes when kept, the mesh bytes unchanged (" + tile.MeshBytes + ")");
            TerrainChunkMesh c0 = tile.Chunks[0];
            var dst = new float[TerrainMcnr.Bytes];
            bool fromFile = TerrainMcnr.Fill(c0, dst);
            bool built = c0.McnrRaw != null && ReferenceEquals(c0.McnrRaw, root.Mcnk[0].NormalsRaw) && c0.McnrRaw[5] == 5 &&
                         dst[3] == (float)(-4 / 127.0) && dst[4] == (float)(5 / 127.0) && dst[5] == (float)(3 / 127.0);
            sbyte[] keep = c0.McnrRaw;
            c0.McnrRaw = null;
            bool fallback = !TerrainMcnr.Fill(c0, dst) && dst.SequenceEqual(c0.Normals);
            c0.McnrRaw = keep;
            Check(fromFile && built && fallback,
                  "MCNR through the parser and the mesh builder: a Built chunk carries the parser's MCNR array (the same array, not a copy) and " +
                  "decodes it (vertex 1: bytes 3, 4, 5 -> (-4, 5, 3) / 127); a chunk without MCNR takes its geometry normals (Fill says false)");
        }

        static void TerrainLightTests()
        {
            var albedo = new[] { 1.0, 1.0, 1.0 };
            var up = new[] { 0.0, 1.0, 0.0 };
            var ambient = new[] { 0.5, 0.4, 0.3 };
            var horizon = new[] { 0.2, 0.2, 0.2 };
            var ground = new[] { 0.1, 0.0, 0.0 };
            var direct = new[] { 0.6, 0.3, 0.0 };
            var rgb = new double[3];
            // N.L = 1, 0, -1 with N up: A = Ambient x 1.1 / 0.9 / 0.7 (the unsaturated wrap), D = Direct x max(N.L, 0).
            TerrainOutdoorLight.Shade(albedo, up, new[] { 0.0, 1.0, 0.0 }, ambient, horizon, ground, direct, false, rgb);
            bool nl1 = Near(rgb[0], 0.5 * 1.1 + 0.6) && Near(rgb[1], 0.4 * 1.1 + 0.3) && Near(rgb[2], 0.3 * 1.1);
            TerrainOutdoorLight.Shade(albedo, up, new[] { 1.0, 0.0, 0.0 }, ambient, horizon, ground, direct, false, rgb);
            bool nl0 = Near(rgb[0], 0.5 * 0.9) && Near(rgb[2], 0.3 * 0.9);
            TerrainOutdoorLight.Shade(albedo, up, new[] { 0.0, -1.0, 0.0 }, ambient, horizon, ground, direct, false, rgb);
            bool nlm1 = Near(rgb[0], 0.5 * 0.7) && Near(rgb[1], 0.4 * 0.7);
            Check(nl1 && nl0 && nlm1,
                  "terrain light (hemisphere, PROVEN_BYTES 2977247): the wrap 0.7 + 0.4 (0.5 + 0.5 N.L) NOT saturated -- 1.1 / 0.9 / 0.7 of H at " +
                  "N.L 1 / 0 / -1 (MEASURED 1.104 / 0.907 / 0.710) -- plus Direct x max(N.L, 0)");
            // H: a level normal takes the horizon colour, a downward one the ground's, a zero horizon or ground falls back to Ambient.
            var level = new[] { 1.0, 0.0, 0.0 };
            var down = new[] { 0.0, -1.0, 0.0 };
            var l0 = new[] { 0.0, 0.0, 1.0 };                       // N.L 0 for both: A = 0.9 H, D = 0
            TerrainOutdoorLight.Shade(albedo, level, l0, ambient, horizon, ground, direct, false, rgb);
            bool h = Near(rgb[0], 0.9 * 0.2);
            TerrainOutdoorLight.Shade(albedo, down, l0, ambient, horizon, ground, direct, false, rgb);
            bool g = Near(rgb[0], 0.9 * 0.1) && Near(rgb[1], 0.0);
            TerrainOutdoorLight.Shade(albedo, level, l0, ambient, new[] { 0.0, 0.0, 0.0 }, ground, direct, false, rgb);
            bool hz = Near(rgb[0], 0.9 * 0.5);
            TerrainOutdoorLight.Shade(albedo, down, l0, ambient, horizon, new[] { 0.0, 0.0, 0.0 }, direct, false, rgb);
            bool gz = Near(rgb[0], 0.9 * 0.5);
            var halfUp = new[] { Math.Sqrt(0.75), 0.5, 0.0 };     // N.U 0.5: H half-way from the horizon to the ambient colour
            TerrainOutdoorLight.Shade(albedo, halfUp, l0, ambient, horizon, ground, direct, false, rgb);
            bool mid = Near(rgb[0], 0.9 * (0.2 + (0.5 - 0.2) * 0.5));
            Check(h && g && hz && gz && mid,
                  "terrain light H: N.U >= 0 lerps the horizon colour to AmbientColor, N.U < 0 to GroundAmbientColor; a zero horizon or ground " +
                  "colour falls back to AmbientColor (INFERRED (public))");
            // The albedo is not saturated; the normal is used at its own length (the client's: the interpolated MCNR / 127 vector,
            // 0.994 long at the Southfury median), not normalised; the flat variant.
            const double s = 0.994;
            TerrainOutdoorLight.Shade(new[] { 1.6, 0.5, 0.0 }, new[] { 0.0, s, 0.0 }, new[] { 0.0, 1.0, 0.0 }, ambient, horizon, ground, direct, false, rgb);
            double wrapS = 0.7 + 0.4 * (0.5 + 0.5 * s);
            bool unclamped = Near(rgb[0], 1.6 * ((0.2 + (0.5 - 0.2) * s) * wrapS + 0.6 * s)) && Near(rgb[1], 0.5 * ((0.2 + (0.4 - 0.2) * s) * wrapS + 0.3 * s));
            double asIs = rgb[0];
            TerrainOutdoorLight.Shade(new[] { 1.6, 0.5, 0.0 }, new[] { 0.0, 1.0, 0.0 }, new[] { 0.0, 1.0, 0.0 }, ambient, horizon, ground, direct, false, rgb);
            double unit = rgb[0];
            bool notNormalised = unit - asIs > 0.005 && Near(unit, 1.6 * (0.5 * 1.1 + 0.6));
            TerrainOutdoorLight.Shade(albedo, up, new[] { 0.0, 1.0, 0.0 }, ambient, horizon, ground, direct, true, rgb);
            bool flat = Near(rgb[0], 1.0) && Near(rgb[1], 0.7) && Near(rgb[2], 0.3);
            Check(unclamped && notNormalised && flat && TerrainOutdoorLight.VertexColourFactor == 2.0 &&
                  TerrainOutdoorLight.NoVertexColour * TerrainOutdoorLight.VertexColourFactor == 1.0,
                  "terrain light: an albedo above 1 is lit as it is (no saturate before the light); the normal is used at its own length as the " +
                  "client's program uses it, not normalised (0.994 long: red " + F(asIs) + ", against " + F(unit) + " for a unit normal); the flat " +
                  "class (for the record) albedo x sat(Direct x max(N.L, 0) + Ambient) x (0.7 + 0.3 x 1); MCCV x 2, 0.5 without MCCV (factor 1)");
        }

        // ================================================================================== the Southfury tiles (WMV_MCNR_TILES)

        sealed class Tile
        {
            public int X, Y;
            public uint Id;
            public string Sha;
            public TerrainTileMesh Mesh;
        }

        static readonly Tile[] Southfury =
        {
            new Tile { X = 39, Y = 29, Id = 786420, Sha = "118ffa05bb670d8a7a89335fd2b422246bb4e5ec98f236c7d4c4b93b8e9a3798" },
            new Tile { X = 40, Y = 29, Id = 786640, Sha = "1c86d137369611bfa4c8e207db22a96a39c37d4c36dbfee0d7d7836c4b8d3b1b" },
            new Tile { X = 39, Y = 30, Id = 786425, Sha = "8f409d61277ea22ef42b5604fb10bd5f0d98f4bf13a932ed438e2539e8d3395d" },
            new Tile { X = 40, Y = 30, Id = 786645, Sha = "65ad571067679b3a23c92a6d39f7e81d2c94b2edf03ad06583701ffc70a196b6" },
        };

        static double Angle(double ax, double ay, double az, double bx, double by, double bz)
        {
            double la = Math.Sqrt(ax * ax + ay * ay + az * az), lb = Math.Sqrt(bx * bx + by * by + bz * bz);
            if (la <= 0 || lb <= 0) return 180.0;
            double d = (ax * bx + ay * by + az * bz) / (la * lb);
            return Math.Acos(Math.Max(-1.0, Math.Min(1.0, d))) * 180.0 / Math.PI;
        }

        /// <summary>Is MCVT entry k of chunk (ix, iy) an outer vertex on the tile's edge (its geometry normal one-sided)?</summary>
        static bool TileEdge(int ix, int iy, int k)
        {
            int r = k / 17, j = k % 17;
            if (j >= 9)
                return false;
            return (iy == 0 && r == 0) || (iy == 15 && r == 8) || (ix == 0 && j == 0) || (ix == 15 && j == 8);
        }

        static void SouthfuryTests(string folder)
        {
            foreach (Tile t in Southfury)
            {
                string path = Path.Combine(folder, t.Id.ToString(CultureInfo.InvariantCulture) + ".bin");
                string sha = File.Exists(path) ? TerrainCorpusTests.Sha256(path) : "missing";
                Check(sha == t.Sha, "Southfury tile " + t.X + "_" + t.Y + " (" + t.Id + ".bin) has its pinned SHA-256 (" + sha.Substring(0, Math.Min(16, sha.Length)) + ")");
                if (sha != t.Sha)
                    return;
                AdtRoot root = AdtParser.ParseRoot(File.ReadAllBytes(path), t.X, t.Y, t.Id + ".bin");
                t.Mesh = TerrainMeshBuilder.Build(root, t.X, t.Y, WorldToUnity.TileOrigin(t.X, t.Y), true);
            }

            // The decode against the geometry normals, every vertex away from the tile edges; and every axis order and sign.
            var angles = new List<double>();
            int noMcnr = 0;
            var perms = new[] { new[] { 0, 1, 2 }, new[] { 0, 2, 1 }, new[] { 1, 0, 2 }, new[] { 1, 2, 0 }, new[] { 2, 0, 1 }, new[] { 2, 1, 0 } };
            var sums = new double[48];
            int counted = 0;
            var dst = new float[TerrainMcnr.Bytes];
            foreach (Tile t in Southfury)
                foreach (TerrainChunkMesh c in t.Mesh.Chunks)
                {
                    if (c.State != TerrainChunkState.Built || c.Triangles == 0)
                        continue;
                    if (!TerrainMcnr.Fill(c, dst))
                    {
                        noMcnr++;
                        continue;
                    }
                    for (int k = 0; k < TerrainVertexLayout.VertexCount; k++)
                    {
                        if (TileEdge(c.IndexX, c.IndexY, k))
                            continue;
                        double gx = c.Normals[3 * k], gy = c.Normals[3 * k + 1], gz = c.Normals[3 * k + 2];
                        angles.Add(Angle(dst[3 * k], dst[3 * k + 1], dst[3 * k + 2], gx, gy, gz));
                        if (t.X == 39 && t.Y == 29)
                        {
                            // Candidate: game (X, Y, Z) = signs x bytes in an order, to Unity (-Y, Z, X).
                            counted++;
                            int m = 0;
                            foreach (int[] o in perms)
                                for (int sgn = 0; sgn < 8; sgn++, m++)
                                {
                                    double bx = c.McnrRaw[3 * k + o[0]] * ((sgn & 1) != 0 ? -1 : 1);
                                    double by = c.McnrRaw[3 * k + o[1]] * ((sgn & 2) != 0 ? -1 : 1);
                                    double bz = c.McnrRaw[3 * k + o[2]] * ((sgn & 4) != 0 ? -1 : 1);
                                    sums[m] += Angle(-by, bz, bx, gx, gy, gz);
                                }
                        }
                    }
                }
            angles.Sort();
            double mean = angles.Count > 0 ? angles.Average() : 180.0, median = angles.Count > 0 ? angles[angles.Count / 2] : 180.0;
            double p95 = angles.Count > 0 ? angles[(int)(0.95 * (angles.Count - 1))] : 180.0;
            var means = sums.Select(x => counted > 0 ? x / counted : 180.0).ToArray();
            int best = Array.IndexOf(means, means.Min());
            double second = means.Where((x, i) => i != best).Min();
            Check(mean < 3.0 && noMcnr == 0 && best == 0 && second > 3.0 * means[0],
                  "MCNR on the four Southfury tiles (Retail 12.1.5): the decode (-b1, b2, b0) / 127 against the geometry normals over " + angles.Count +
                  " vertices away from the tile edges -- mean " + F(mean, "0.###") + " deg (under 3), median " + F(median, "0.###") + ", p95 " +
                  F(p95, "0.###") + "; of the 48 axis orders and signs on 39_29 it is the best (" + F(means[0], "0.###") + " deg), the next " +
                  F(second, "0.###") + " deg; " + noMcnr + " chunk(s) without MCNR");

            // The lengths the light uses (the decoded vectors are not normalised, as in the client): every vertex of every chunk.
            var lengths = new List<double>();
            foreach (Tile t in Southfury)
                foreach (TerrainChunkMesh c in t.Mesh.Chunks)
                    if (c.State == TerrainChunkState.Built && TerrainMcnr.Fill(c, dst))
                        for (int k = 0; k < TerrainVertexLayout.VertexCount; k++)
                            lengths.Add(Math.Sqrt(dst[3 * k] * dst[3 * k] + dst[3 * k + 1] * dst[3 * k + 1] + dst[3 * k + 2] * dst[3 * k + 2]));
            lengths.Sort();
            double lmin = lengths.Count > 0 ? lengths[0] : 0, lmax = lengths.Count > 0 ? lengths[lengths.Count - 1] : 0;
            double lmed = lengths.Count > 0 ? lengths[lengths.Count / 2] : 0, lp1 = lengths.Count > 0 ? lengths[lengths.Count / 100] : 0;
            Check(lengths.Count == 4 * 256 * 145 && lmin > 0.97 && lmax < 1.001 && lmed > 0.99 && lmed < 0.999,
                  "MCNR lengths on the four Southfury tiles, as the light uses them (byte / 127, INFERRED (public); not normalised): " + lengths.Count +
                  " vectors from " + F(lmin, "0.####") + " to " + F(lmax, "0.####") + ", p1 " + F(lp1, "0.####") + ", median " + F(lmed, "0.####") +
                  " (MEASURED; 1 - median " + F(100 * (1 - lmed), "0.0") + " %)");

            // The seams: both tiles' MCNR at every shared outer vertex.
            var seams = new[] { new[] { 0, 1, 1 }, new[] { 2, 3, 1 }, new[] { 0, 2, 0 }, new[] { 1, 3, 0 } };   // tile a, tile b, 1: a column seam
            double seamWorst = 0.0;
            int shared = 0;
            var da = new float[TerrainMcnr.Bytes];
            var db = new float[TerrainMcnr.Bytes];
            var each = new List<string>();
            foreach (int[] sm in seams)
            {
                Tile a = Southfury[sm[0]], b = Southfury[sm[1]];
                bool column = sm[2] == 1;
                double w = 0.0;
                for (int ci = 0; ci < 16; ci++)
                {
                    // Column seam: a's chunks (15, ci) column 8 meet b's (0, ci) column 0; row seam: a's (ci, 15) row 8 meet b's (ci, 0) row 0.
                    TerrainChunkMesh ca = column ? a.Mesh.Chunks[ci * 16 + 15] : a.Mesh.Chunks[15 * 16 + ci];
                    TerrainChunkMesh cb = column ? b.Mesh.Chunks[ci * 16 + 0] : b.Mesh.Chunks[0 * 16 + ci];
                    if (!TerrainMcnr.Fill(ca, da) || !TerrainMcnr.Fill(cb, db))
                        continue;
                    for (int q = 0; q <= 8; q++)
                    {
                        int ka = column ? TerrainVertexLayout.Outer(q, 8) : TerrainVertexLayout.Outer(8, q);
                        int kb = column ? TerrainVertexLayout.Outer(q, 0) : TerrainVertexLayout.Outer(0, q);
                        double ang = Angle(da[3 * ka], da[3 * ka + 1], da[3 * ka + 2], db[3 * kb], db[3 * kb + 1], db[3 * kb + 2]);
                        w = Math.Max(w, ang);
                        shared++;
                    }
                }
                seamWorst = Math.Max(seamWorst, w);
                each.Add(a.X + "_" + a.Y + "|" + b.X + "_" + b.Y + " " + F(w, "0.##"));
            }
            Check(seamWorst < 1.0 && shared == 4 * 16 * 9,
                  "MCNR across the four Southfury tile seams: the two tiles' normals at each of the " + shared + " shared vertex copies within " +
                  F(seamWorst, "0.###") + " deg (under 1; " + string.Join(", ", each.ToArray()) + ") -- this proves the data continuous across " +
                  "the seams and the seam vertices matched, NOT the decode (an angle is the same under any axis order or sign); the 48-mapping " +
                  "check above proves the decode");
        }

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();
            log.Add("Sky: the colour dome");
            DomeTests();
            log.Add("Sky: the day curve, the inputs, the sky program and its CPU twin");
            SkyProgramTests();
            log.Add("Terrain light: the MCNR decode");
            McnrTests();
            log.Add("Terrain light: the client's formula");
            TerrainLightTests();
            string tiles = Environment.GetEnvironmentVariable("WMV_MCNR_TILES");
            log.Add("Terrain light: MCNR on the four Southfury tiles");
            if (string.IsNullOrEmpty(tiles))
                log.Add("  SKIPPED: WMV_MCNR_TILES is not set (the four root ADTs are extracted locally, not committed)");
            else
                SouthfuryTests(tiles);
            log.Add(string.Format("SkyTerrainLightTests: {0} failure(s)", failures));
            if (output != null)
                foreach (string line in log)
                    output(line);
            return failures;
        }
    }
}
