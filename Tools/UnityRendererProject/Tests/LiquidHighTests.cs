// LiquidHighTests.cs
//
// Unit tests of the High water look's pure parts (Wow/World/LiquidHigh.cs): the HIGH path's formulas on numbers -- the
// transmission T(Ls) with the start values (the tuned defaults) against hand-computed values, the water colour W with its depth
// ramp and gain (the zone light's) and without a zone light (the type's colour x noZoneGain, saturated: Classic Beta's PBR
// water never clips past 1), Schlick's Fresnel, the refraction ramp and the rule that accepts the shifted sample (behind
// the water point and not above the water), the refracted colour, a reflection texel's coverage (the background replaced by
// the sky), the reflection mix (the Fresnel share at normal incidence; the two shares scaled to a sum of at most 1, the
// output saturated) and the ripple map's offset for the floating origin (two entry origins, one ripple position); the
// -wmvWaterTune string (start values, overrides, every reason a token is ignored, the log line) and which command-line
// and WMV_DEBUG tokens are its pairs; which side of a sheet the camera sees (the HIGH path by the triangle's facing:
// a sloped sheet rising above the eye, seen from above and from below; flat water; no normal); and the ripple map's
// unit-normal check (a gentle unit normal map passes, colour noise and an all-black map fail, the sampling grid) with its
// slope spread, strength factor and mean slope (taken out of every normal: mean 0 after, the spread kept). Framework-free
// like the other suites: RunAll returns the failure count.

using System;
using System.Collections.Generic;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class LiquidHighTests
    {
        static int failures;
        static readonly List<string> log = new List<string>();

        static void Check(bool condition, string name)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name); failures++; }
        }

        static bool Near(double a, double b, double eps = 1e-9) { return Math.Abs(a - b) <= eps; }

        static double T(double ls)
        {
            var t = new LiquidHighTune();
            return LiquidHighMath.Transmission(ls, t[LiquidHighTune.TFastA], t[LiquidHighTune.TFastL], t[LiquidHighTune.TSlowA], t[LiquidHighTune.TSlowL]);
        }

        // ================================================================================== the formulas

        static void FormulaTests()
        {
            // Hand values: min(1, 1.09 exp(-Ls / 0.154) + 0.38 exp(-Ls / 24.9)).
            Check(T(0) == 1.0 && T(0.03) == 1.0 && Near(T(0.12), 0.8782251319497718, 1e-12) && Near(T(1), 0.36669076555836, 1e-12) &&
                  Near(T(3.25), 0.3335021014718372, 1e-12) && Near(T(10), 0.2543127546319706, 1e-12) && Near(T(100), 0.006849029721139382, 1e-12),
                  "T(Ls) = min(1, 1.09 exp(-Ls/0.154) + 0.38 exp(-Ls/24.9)): 1 at 0 and 0.03 (the sum passes 1), 0.8782 at 0.12, 0.3667 at 1, " +
                  "0.3335 at 3.25, 0.2543 at 10, 0.0068 at 100 yd");
            bool falling = true;
            for (double l = 0.1; l < 200; l *= 1.5)
                falling &= T(l * 1.5) < T(l);
            Check(falling && T(-1) == T(0) && T(1e6) >= 0 && T(1e6) < 1e-9,
                  "T falls with Ls past the clamp, a negative Ls counts as 0, and it tends to 0");
            Check(LiquidHighMath.Water(0.2, 0.6, 0, 3.25, 1) == 0.2 && Near(LiquidHighMath.Water(0.2, 0.6, 1.625, 3.25, 1), 0.4) &&
                  LiquidHighMath.Water(0.2, 0.6, 3.25, 3.25, 1) == 0.6 && LiquidHighMath.Water(0.2, 0.6, 30, 3.25, 1) == 0.6 &&
                  Near(LiquidHighMath.Water(0.2, 0.6, 1.625, 3.25, 1.5), 0.6) && LiquidHighMath.Water(0.2, 0.6, -1, 3.25, 1) == 0.2,
                  "W = lerp(close, far, saturate(Ls / 3.25)) x gain: close at 0, halfway at 1.625, far from 3.25 on, the gain applied after");
            Check(LiquidHighMath.Schlick(1, 0.02) == 0.02 && LiquidHighMath.Schlick(0, 0.02) == 1.0 && Near(LiquidHighMath.Schlick(0.5, 0.02), 0.050625) &&
                  LiquidHighMath.Schlick(2, 0.02) == 0.02 && LiquidHighMath.Schlick(-1, 0.02) == 1.0,
                  "Schlick: F0 (0.02) at normal incidence, 1 at grazing, 0.0506 at cos 0.5; cos outside 0..1 clamped");
            Check(LiquidHighMath.RefractRamp(0, 0.3) == 0 && Near(LiquidHighMath.RefractRamp(0.15, 0.3), 0.5) && LiquidHighMath.RefractRamp(0.3, 0.3) == 1 &&
                  LiquidHighMath.RefractRamp(5, 0.3) == 1,
                  "the refraction offset's ramp: none at Ls 0, half at 0.15 yd, full from 0.3 yd");
            Check(Near(LiquidHighMath.Refracted(0.2, 0.9, 1), 0.9) && LiquidHighMath.Refracted(0.2, 0.9, 0) == 0.2 && Near(LiquidHighMath.Refracted(0.2, 0.9, 0.5), 0.55),
                  "refracted = lerp(W, bed, T): the bed at T 1, the water colour at T 0");
            double f = LiquidHighMath.Schlick(1, 0.02);
            Check(LiquidHighMath.Mix(0.3, 0.9, 0.6, 0, 0) == 0.3 && Near(LiquidHighMath.Mix(0.3, 0.9, 0.6, 0.495 * f, 0.168 * f), 0.306948, 1e-12) &&
                  Near(LiquidHighMath.Mix(0, 1, 1, 0.495 * f, 0.168 * f), 0.495 * 0.02 + 0.168 * 0.02) && LiquidHighMath.Mix(0.3, 0.9, 0.6, 1, 0) == 0.9,
                  "the mix refracted (1 - rb - rd) + blur rb + sharp rd: no reflection at 0 gains; at normal incidence the reflection's share is " +
                  "broad x 0.02 + detail x 0.02 (0.01326); a gain of 1 on Schlick 1 is the reflection alone");

            // H10: the shares scaled to a sum of at most 1, the output saturated. Before, reflBroad 0.8 + reflDetail 0.5 at
            // grazing gave the refracted colour a weight of -0.3 (0.5 -> -0.15 with a black reflection).
            double rb = 0.8, rd = 0.5;
            LiquidHighMath.LimitReflection(ref rb, ref rd);
            double rb2 = 0.3, rd2 = 0.2, rb3 = -0.2, rd3 = 0.4;
            LiquidHighMath.LimitReflection(ref rb2, ref rd2);
            LiquidHighMath.LimitReflection(ref rb3, ref rd3);
            Check(Near(rb, 0.8 / 1.3, 1e-15) && Near(rd, 0.5 / 1.3, 1e-15) && Near(rb + rd, 1.0, 1e-15) && rb2 == 0.3 && rd2 == 0.2 && rb3 == 0 && rd3 == 0.4 &&
                  Near(LiquidHighMath.Mix(0.5, 0, 0, 0.8, 0.5), 0.0, 1e-15) && Near(LiquidHighMath.Mix(0.5, 0.6, 0.2, 0.8, 0.5), 0.6 * 0.8 / 1.3 + 0.2 * 0.5 / 1.3, 1e-15) &&
                  LiquidHighMath.Mix(1.2, 1.5, 1.5, 0.1, 0) == 1.0 && LiquidHighMath.Mix(-0.2, 0, 0, 0, 0) == 0.0 &&
                  Near(LiquidHighMath.Mix(0.4, 0.8, 0.6, 0.6, 0.4), 0.8 * 0.6 + 0.6 * 0.4, 1e-15),
                  "H10: rb + rd above 1 are scaled to sum 1 (0.8 + 0.5 -> 0.615 + 0.385, the refracted share 0, never negative), below 1 kept, a " +
                  "negative share 0; the output is saturated to 0..1 (1.2 refracted -> 1, -0.2 -> 0)");

            // H3: without a zone light W = saturate(MinimapStaticCol x noZoneGain); the zone gain never touches the type colour.
            double[] beta = { 182 / 255.0, 244 / 255.0, 245 / 255.0 }, retail = { 18 / 255.0, 160 / 255.0, 213 / 255.0 };
            Check(Near(LiquidHighMath.WaterNoZone(beta[0], 0.5), 91 / 255.0, 1e-15) && Near(LiquidHighMath.WaterNoZone(beta[2], 0.5), 122.5 / 255.0, 1e-15) &&
                  Near(LiquidHighMath.WaterNoZone(beta[0], 1.35), 245.7 / 255.0, 1e-12) && LiquidHighMath.WaterNoZone(beta[1], 1.35) == 1.0 &&
                  LiquidHighMath.WaterNoZone(beta[2], 1.35) == 1.0 && LiquidHighMath.WaterNoZone(retail[2], 1.35) == 1.0 &&
                  Near(LiquidHighMath.WaterNoZone(retail[1], 0.5), 80 / 255.0, 1e-15) && LiquidHighMath.WaterNoZone(0.4, 0) == 0.0 &&
                  LiquidHighMath.WaterNoZone(0.4, 1) == 0.4 && new LiquidHighTune()[LiquidHighTune.NoZoneGain] == 0.3125,
                  "H3: without a zone light W = saturate(type colour x noZoneGain) (default 0.3125) -- Classic Beta's (182, 244, 245) at 0.5 is " +
                  "(91, 122, 122.5), and even at the zone gain 1.35 it stops at 1 (green and blue clip there, never past it)");

            // H1: the shifted sample is the bed only behind the water point AND not above the water (0.02 yd tolerance).
            Check(LiquidHighMath.AcceptRefracted(1.0, 49.0, 50.0) && LiquidHighMath.AcceptRefracted(0.0, 50.01, 50.0) &&
                  !LiquidHighMath.AcceptRefracted(1.0, 50.5, 50.0) && !LiquidHighMath.AcceptRefracted(1.0, 50.0201, 50.0) &&
                  !LiquidHighMath.AcceptRefracted(-0.1, 49.0, 50.0) && !LiquidHighMath.AcceptRefracted(-1.0, 55.0, 50.0) &&
                  LiquidHighMath.AboveWaterTolerance == 0.02,
                  "H1: the shifted refraction sample is accepted behind the water point and at most 0.02 yd above it; a surface 0.5 yd above the " +
                  "water behind P (the far bank, a trunk) or anything in front of P keeps the unshifted sample");

            // H2: a reflection texel premultiplied by its coverage, the sky filling the rest.
            Check(LiquidHighMath.Reflected(0.7, 1, 0.3) == 0.7 && LiquidHighMath.Reflected(0, 0, 0.3) == 0.3 &&
                  Near(LiquidHighMath.Reflected(0.35, 0.5, 0.3), 0.5, 1e-15) && LiquidHighMath.Reflected(0.7, 1.5, 0.3) == 0.7 &&
                  Near(LiquidHighMath.Reflected(0.25 * 0.8, 0.25, 0.4), 0.25 * 0.8 + 0.75 * 0.4, 1e-15),
                  "H2: a reflection texel reads c + sky (1 - coverage): a scene texel itself, a background texel the sky, a texel half over the " +
                  "silhouette half scene half sky (never the camera's clear colour)");

            // H5: the ripple map's offset. The same game point seen from two entry origins (tile 30_49's and 31_49's corners,
            // 533.33 yd apart: 88.9 repeats of tile A) samples the ripple map at the same place modulo each tile.
            double gx = -9093.75, gy = 287.5;                           // a game point (x north, y west)
            double[][] origins = { new[] { (32 - 49) * 1600.0 / 3.0, (32 - 30) * 1600.0 / 3.0 }, new[] { (32 - 49) * 1600.0 / 3.0, (32 - 31) * 1600.0 / 3.0 },
                                   new[] { (32 - 48) * 1600.0 / 3.0, (32 - 29) * 1600.0 / 3.0 } };
            bool same = true;
            string seen = "";
            foreach (double tile in new[] { 6.0, 20.0, 7.3 })
            {
                double firstU = double.NaN, firstV = double.NaN;
                foreach (double[] o in origins)
                {
                    float ux = (float)(-(gy - o[1])), uz = (float)(gx - o[0]);   // WorldToUnity.Position's x and z
                    double ox, oz;
                    LiquidHighMath.RippleOffset(o[0], o[1], tile, out ox, out oz);
                    same &= ox >= 0 && ox < tile && oz >= 0 && oz < tile;
                    double u = LiquidHighMath.Mod((float)ox + ux, tile), v = LiquidHighMath.Mod((float)oz + uz, tile);
                    if (double.IsNaN(firstU))
                    {
                        firstU = u;
                        firstV = v;
                    }
                    double du = Math.Abs(u - firstU), dv = Math.Abs(v - firstV);
                    same &= Math.Min(du, tile - du) < 1e-3 && Math.Min(dv, tile - dv) < 1e-3;
                    seen += string.Format(System.Globalization.CultureInfo.InvariantCulture, " {0}:({1:0.000},{2:0.000})", tile, u, v);
                }
                same &= Near(firstU, LiquidHighMath.Mod(-gy, tile), 1e-3) && Near(firstV, LiquidHighMath.Mod(gx, tile), 1e-3);
            }
            Check(same && LiquidHighMath.Mod(-1, 6) == 5 && LiquidHighMath.Mod(13, 6) == 1 && LiquidHighMath.Mod(-12, 6) == 0 && LiquidHighMath.Mod(0, 20) == 0,
                  "H5: the ripple offset (the origin's (-Y, X) modulo each tile, in double) puts a game point at the same ripple position from " +
                  "every entry origin (tiles 6, 20 and 7.3 yd; three origins up to 533 yd apart) -- the absolute (-Y, X) modulo the tile;" + seen);

            // F1: the HIGH path by the triangle's facing (UpperSide). A sheet rising 0.5 yd per yd towards +x, a pixel's screen
            // derivatives (2, 1, 0) and (0, 0, -2) (or +2: either vertical sense). From 40 yd west at height 50 the rays to its parts at
            // 50.5 and 51.5 yd rise, yet meet the upper side: HIGH, where the old test (the ray going down) said no. From 40 yd east,
            // the eye below the sheet's plane: never HIGH, even where the ray runs down.
            double[] sx = { 2, 1, 0 }, sySouth = { 0, 0, -2 }, syNorth = { 0, 0, 2 };
            bool f1 = LiquidHighMath.ViewDown == 1e-4 && LiquidHighMath.FacingMin == 1e-12;
            string sides = "";
            foreach (double h in new[] { 48.5, 49.5, 50.5, 51.5 })
            {
                double l = Math.Sqrt(1600 + (h - 50) * (h - 50)), wx = 40 / l, wy = (h - 50) / l;
                bool west = Side(wx, wy, 0, sx, sySouth) && Side(wx, wy, 0, sx, syNorth);
                bool east = Side(-wx, wy, 0, sx, sySouth) || Side(-wx, wy, 0, sx, syNorth);
                bool old = -wy > LiquidHighMath.ViewDown;
                f1 &= west && !east && old == (h < 50);
                sides += string.Format(System.Globalization.CultureInfo.InvariantCulture, " {0}: old {1}, west {2}, east {3};", h, old, west, east);
            }
            Check(f1, "F1: the HIGH path by the triangle's facing -- a sheet rising 0.5 yd per yd, seen from 40 yd west: HIGH all along, its " +
                      "far part above the eye too (the old test: only below the eye); from 40 yd east, below its plane: never, even where the " +
                      "ray runs down;" + sides);
            // Flat water: exactly the old test, around its 1e-4 threshold too (derivatives with x and z parts, either order).
            double[] fx = { 2, 0, 0.5 }, fy = { -0.3, 0, -2 };
            bool flatSame = true;
            foreach (double vy in new[] { -0.9, -0.01, -2e-4, -1.0001e-4, -0.9999e-4, -5e-5, 0, 1e-3, 0.6 })
            {
                double vx = Math.Sqrt(1 - vy * vy);
                bool old = -vy > LiquidHighMath.ViewDown;
                flatSame &= Side(vx, vy, 0, fx, fy) == old && Side(vx, vy, 0, fy, fx) == old && Side(0, vy, vx, fx, fy) == old;
            }
            // No normal (a derivative 0, the two parallel: the sheet edge-on, |nUp| <= 1e-12): the old test -- on the sloped sheet's
            // far part seen from the west that is "not HIGH"; the same derivatives 100 x larger (|nUp| 1.1e-9) give the facing.
            double[] zero = { 0, 0, 0 }, par = { 4, 2, 0 }, tinyX = { 1e-6, 0.5e-6, 0 }, tinyY = { 0, 0, 1e-7 }, smallX = { 1e-4, 0.5e-4, 0 },
                     smallY = { 0, 0, 1e-5 };
            double l2 = Math.Sqrt(1600 + 1.5 * 1.5), wux = 40 / l2, wuy = 1.5 / l2;
            bool degenerate = true;
            foreach (double vy in new[] { -0.5, -2e-4, -5e-5, 0.5 })
            {
                double vx = Math.Sqrt(1 - vy * vy);
                bool old = -vy > LiquidHighMath.ViewDown;
                degenerate &= Side(vx, vy, 0, zero, sySouth) == old && Side(vx, vy, 0, sx, zero) == old && Side(vx, vy, 0, sx, par) == old &&
                              Side(vx, vy, 0, tinyX, tinyY) == old;
            }
            degenerate &= !Side(wux, wuy, 0, tinyX, tinyY) && Side(wux, wuy, 0, smallX, smallY);
            Check(flatSame && degenerate,
                  "F1: on flat water the facing test is the old one exactly (-v.y > 1e-4, at the threshold too); where the derivatives give " +
                  "no normal (one 0, the two parallel, |nUp| 1.1e-13) the old test decides, from |nUp| 1.1e-9 the facing");

            // The mean tilt taken out (RemoveTilt): the slope less the mean, z kept; linear, so the blend of two samples' results is
            // the blend's result.
            double ax = 0.3, ay = -0.1, az = 0.95, bx = -0.2, by = 0.25, bz = 0.9, wA = 0.4;
            double rax = ax, ray = ay, rbx = bx, rby = by;
            LiquidHighMath.RemoveTilt(ref rax, ref ray, az, 0.05, -0.02);
            LiquidHighMath.RemoveTilt(ref rbx, ref rby, bz, 0.05, -0.02);
            double mx2 = wA * ax + (1 - wA) * bx, my2 = wA * ay + (1 - wA) * by, mz2 = wA * az + (1 - wA) * bz;
            LiquidHighMath.RemoveTilt(ref mx2, ref my2, mz2, 0.05, -0.02);
            Check(Near(rax / az, ax / az - 0.05, 1e-15) && Near(ray / az, ay / az + 0.02, 1e-15) && Near(mx2, wA * rax + (1 - wA) * rbx, 1e-15) &&
                  Near(my2, wA * ray + (1 - wA) * rby, 1e-15),
                  "mean tilt: RemoveTilt takes the mean slope out of a normal's slope (z kept), and taking it out of the blend of two samples " +
                  "is taking it out of each (the shader does it once, on the blend)");
        }

        static bool Side(double vx, double vy, double vz, double[] dx, double[] dy)
        {
            return LiquidHighMath.UpperSide(vx, vy, vz, dx[0], dx[1], dx[2], dy[0], dy[1], dy[2]);
        }

        // ================================================================================== the tune

        static void TuneTests()
        {
            var start = new LiquidHighTune();
            bool starts = start[LiquidHighTune.Refract] == 0.02 && start[LiquidHighTune.TFastA] == 1.09 && start[LiquidHighTune.TFastL] == 0.154 &&
                          start[LiquidHighTune.TSlowA] == 0.38 && start[LiquidHighTune.TSlowL] == 24.9 && start[LiquidHighTune.WaterGain] == 1.35 &&
                          start[LiquidHighTune.ReflBroad] == 0.2 && start[LiquidHighTune.ReflDetail] == 0.02 && start[LiquidHighTune.F0] == 0.02 &&
                          start[LiquidHighTune.BumpTileA] == 6 && start[LiquidHighTune.BumpTileB] == 20 && start[LiquidHighTune.BumpScroll] == 0.05 &&
                          start[LiquidHighTune.BumpWeightA] == 0.4 && start[LiquidHighTune.BumpStrength] == 2 && start[LiquidHighTune.RefractRamp] == 0.3 &&
                          start[LiquidHighTune.ReflMaxDist] == 128 && start[LiquidHighTune.ReflThick] == 0.3 && start[LiquidHighTune.ReflThickPct] == 0.02 &&
                          start[LiquidHighTune.ReflEdge] == 0.2 && start[LiquidHighTune.ReflBump] == 0.25 && start[LiquidHighTune.GrabBlur] == 1 &&
                          start[LiquidHighTune.SkyR] == 0.5 && start[LiquidHighTune.SkyG] == 0.6 && start[LiquidHighTune.SkyB] == 0.7 &&
                          start[LiquidHighTune.BumpMean] == 1.0;
            bool inRange = true;
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (LiquidHighKey k in LiquidHighTune.Keys)
            {
                inRange &= k.Start >= k.Min && k.Start <= k.Max && k.Min < k.Max;
                inRange &= names.Add(k.Key);
            }
            Check(starts && start[LiquidHighTune.NoZoneGain] == 0.3125 && inRange && LiquidHighTune.Keys.Length == 26 && start.Overridden.Count == 0 &&
                  start.Ignored.Count == 0 && LiquidHighTune.Parse(null).Overridden.Count == 0 &&
                  LiquidHighTune.Parse("").Describe() == "none (every value its start value)" &&
                  LiquidHighTune.Parse("noZoneGain:0.73")[LiquidHighTune.NoZoneGain] == 0.73 &&
                  LiquidHighTune.Parse("bumpMean:0")[LiquidHighTune.BumpMean] == 0.0 && LiquidHighTune.Parse("bumpMean:1.5").Ignored.Count == 1,
                  "tune: the tuned start values (refract 0.02, T 1.09/0.154 and 0.38/24.9, gain 1.35, the no-zone gain 0.3125 (the Beta fit), " +
                  "reflection 0.2/0.02, ray tilt 0.25, edge fade 0.2, ripples 6/20 yd, 0.05 yd/s, 0.4, 2, the mean tilt taken out whole), 26 " +
                  "distinct keys each inside its range; nothing given: nothing overridden; noZoneGain and bumpMean (0..1) overridable");

            // The -wmvWaterTune continuation rule (WmvModelBuilder.Debug_): the option's text and every token after it that does
            // not start with '-', up to the next '-' token or the first of WMV_DEBUG's tokens; given twice, the pairs add up.
            var cmdOnly = new List<string> { "UnityRenderer.exe", "-wmvWaterTune=refract:0.03", "tSlowA:0.4", "-wmvAnimTime=0", "x:1" };
            var envSplit = new List<string> { "UnityRenderer.exe", "-wmvLifecycleTest", "-wmvAnimTime=0", "-wmvWaterTune=reflBroad:0.2", "reflDetail:0.02",
                                               "waterGain:1.35" };
            var boundary = new List<string> { "UnityRenderer.exe", "-wmvWaterTune=a:1", "b:2", "c:3" };
            var twice = new List<string> { "x.exe", "-wmvWaterTune=a:1", "-wmvFlipV", "x:1", "-wmvWaterTune=", "b:2", "", "-wmvWaterTune=c:3" };
            var none = new List<string> { "x.exe", "-wmvAnimTime=0", "refract:0.5", "-wmvWaterTun=refract:0.5" };
            var empty = new List<string> { "x.exe", "-wmvWaterTune=" };
            Check(LiquidHighTune.TuneTokens(cmdOnly, cmdOnly.Count) == "refract:0.03,tSlowA:0.4" &&
                  LiquidHighTune.TuneTokens(envSplit, 2) == "reflBroad:0.2,reflDetail:0.02,waterGain:1.35" &&
                  LiquidHighTune.TuneTokens(boundary, 3) == "a:1,b:2" && LiquidHighTune.TuneTokens(boundary, 4) == "a:1,b:2,c:3" &&
                  LiquidHighTune.TuneTokens(twice, twice.Count) == "a:1,b:2,c:3" && LiquidHighTune.TuneTokens(none, none.Count) == null &&
                  LiquidHighTune.TuneTokens(empty, empty.Count) == "" && LiquidHighTune.TuneTokens(null, 0) == null &&
                  LiquidHighTune.Parse(LiquidHighTune.TuneTokens(envSplit, 2)).Overridden.Count == 3,
                  "tune: -wmvWaterTune's pairs -- on the command line, and split by WMV_DEBUG's commas into tokens of their own (every token " +
                  "after the option not starting with '-'); a '-' token ends them, so does the start of WMV_DEBUG's tokens; given twice they " +
                  "add up; a token that only resembles the option, or a pair with no option before it, is none; the option alone is empty");

            LiquidHighTune t = LiquidHighTune.Parse("refract:0.02,TFASTA:1.2;bumpScroll:-0.1 waterGain:1.25,refract:0.03");
            Check(t[LiquidHighTune.Refract] == 0.03 && t[LiquidHighTune.TFastA] == 1.2 && t[LiquidHighTune.BumpScroll] == -0.1 &&
                  t[LiquidHighTune.WaterGain] == 1.25 && t[LiquidHighTune.TFastL] == 0.154 && t.Overridden.Count == 4 && t.Ignored.Count == 0 &&
                  t.Overridden[0] == LiquidHighTune.Refract && t.Overridden[1] == LiquidHighTune.TFastA && t.IsOverridden("tfasta") &&
                  !t.IsOverridden(LiquidHighTune.TFastL) &&
                  t.Describe() == "refract=0.03, tFastA=1.2, bumpScroll=-0.1, waterGain=1.25 (4 overridden; every other value its start value)",
                  "tune: pairs split by commas, semicolons or spaces; keys in any case (the log keeps the key's own spelling); a negative scroll; " +
                  "a key given twice keeps the last value (" + t.Describe() + ")");

            LiquidHighTune bad = LiquidHighTune.Parse("sparkle:1,refract,refract:,:3,refract:abc,refract:NaN,refract:5,bumpTileA:0,tFastL:1e400,skyR:0.4");
            Check(bad[LiquidHighTune.Refract] == 0.02 && bad[LiquidHighTune.BumpTileA] == 6 && bad[LiquidHighTune.TFastL] == 0.154 &&
                  bad[LiquidHighTune.SkyR] == 0.4 && bad.Overridden.Count == 1 && bad.Ignored.Count == 9 &&
                  bad.Ignored[0] == "sparkle:1 (unknown key)" && bad.Ignored[1] == "refract (not key:value)" && bad.Ignored[2] == "refract: (not key:value)" &&
                  bad.Ignored[3] == ":3 (not key:value)" && bad.Ignored[4] == "refract:abc (not a number)" && bad.Ignored[5] == "refract:NaN (not a number)" &&
                  bad.Ignored[6] == "refract:5 (outside 0..1)" && bad.Ignored[7] == "bumpTileA:0 (outside 0.01..1000)" &&
                  bad.Ignored[8] == "tFastL:1e400 (not a number)" && bad.Describe().EndsWith("; ignored: sparkle:1 (unknown key), refract (not key:value), " +
                                                                                             "refract: (not key:value), :3 (not key:value), refract:abc " +
                                                                                             "(not a number), refract:NaN (not a number), refract:5 " +
                                                                                             "(outside 0..1), bumpTileA:0 (outside 0.01..1000), tFastL:1e400 " +
                                                                                             "(not a number)", StringComparison.Ordinal),
                  "tune: an unknown key, a token without a key or value, a value that is not a finite number, or one outside its key's range is " +
                  "ignored and said; the rest still applies (" + bad.Describe() + ")");
            bool threw = false;
            try { double x = start["nope"]; }
            catch (ArgumentException) { threw = true; }
            Check(threw && LiquidHighTune.Find("REFRACT") != null && LiquidHighTune.Find("nope") == null && LiquidHighTune.Find(null) == null,
                  "tune: reading a key that does not exist throws (a typo in the player's own code, never the user's)");
        }

        // ================================================================================== the ripple map's check

        /// <summary>w x h RGBA texels from `texel` (x, y) -> (r, g, b).</summary>
        static byte[] Map(int w, int h, Func<int, int, byte[]> texel)
        {
            var d = new byte[w * h * 4];
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                {
                    byte[] c = texel(x, y);
                    d[4 * (y * w + x)] = c[0];
                    d[4 * (y * w + x) + 1] = c[1];
                    d[4 * (y * w + x) + 2] = c[2];
                    d[4 * (y * w + x) + 3] = 255;
                }
            return d;
        }

        static byte Enc(double n) { return (byte)Math.Max(0, Math.Min(255, Math.Round((n + 1) * 0.5 * 255))); }

        static void BumpTests()
        {
            // A gentle unit normal map (tilts up to about 17 degrees, as the client's newoceanbump), stored as bytes.
            byte[] gentle = Map(512, 512, (x, y) =>
            {
                double ax = 0.3 * Math.Sin(x * 0.19), ay = 0.2 * Math.Cos(y * 0.13 + x * 0.05);
                double nz = Math.Sqrt(Math.Max(0, 1 - ax * ax - ay * ay));
                return new[] { Enc(ax), Enc(ay), Enc(nz) };
            });
            double share;
            int sampled;
            bool unit = LiquidBump.IsUnitNormalMap(gentle, 512, 512, out share, out sampled);
            Check(unit && share == 1.0 && sampled == 64 * 64,
                  "ripple map: a gentle unit normal map passes -- every one of the 64 x 64 texels sampled of a 512 x 512 top mip has |n| in 0.9..1.1");
            var rnd = new Random(7);
            byte[] noise = Map(256, 256, (x, y) => new[] { (byte)rnd.Next(256), (byte)rnd.Next(256), (byte)rnd.Next(256) });
            bool noiseUnit = LiquidBump.IsUnitNormalMap(noise, 256, 256, out share, out sampled);
            Check(!noiseUnit && share < 0.5 && sampled == 64 * 64, "ripple map: colour noise fails (" + share.ToString("0.000") + " of the texels unit)");
            byte[] black = Map(8, 8, (x, y) => new byte[] { 0, 0, 0 });
            byte[] flat = Map(4, 4, (x, y) => new byte[] { 128, 128, 255 });
            double s2, s3;
            int n2, n3;
            bool blackUnit = LiquidBump.IsUnitNormalMap(black, 8, 8, out share, out sampled);
            bool flatUnit = LiquidBump.IsUnitNormalMap(flat, 4, 4, out s2, out n2);
            bool nothing = LiquidBump.IsUnitNormalMap(null, 4, 4, out s3, out n3) || LiquidBump.IsUnitNormalMap(new byte[10], 4, 4, out s3, out n3) ||
                           LiquidBump.IsUnitNormalMap(flat, 0, 4, out s3, out n3);
            Check(!blackUnit && share == 0 && sampled == 64 && flatUnit && s2 == 1 && n2 == 16 && !nothing && n3 == 0,
                  "ripple map: an all-black map fails (|n| = sqrt 3), the flat (128, 128, 255) passes, a map smaller than the grid is read whole; " +
                  "no data, too few bytes or no size: not a normal map, nothing sampled");
            // Just short of the share: 94 % unit fails, 95 % passes (a 10 x 10 map is read whole).
            Func<int, byte[]> partly = units => Map(10, 10, (x, y) => y * 10 + x < units ? new byte[] { 128, 128, 255 } : new byte[] { 0, 0, 0 });
            bool at94 = LiquidBump.IsUnitNormalMap(partly(94), 10, 10, out share, out sampled);
            bool at95 = LiquidBump.IsUnitNormalMap(partly(95), 10, 10, out s2, out n2);
            Check(!at94 && Near(share, 0.94) && at95 && Near(s2, 0.95) && LiquidBump.MinShare == 0.95 && LiquidBump.MinLength == 0.9 && LiquidBump.MaxLength == 1.1,
                  "ripple map: at least 95 % of the texels sampled must be unit (94 % fails, 95 % passes)");

            // H7: the slope spread (sqrt(var(x/z) + var(y/z)) over the unit texels of the grid) and the strength factor.
            // A checkerboard of two tilts along x: the spread is half the two slopes' difference; a constant tilt has none.
            byte[] board = Map(64, 64, (x, y) => (x + y) % 2 == 0 ? new byte[] { 204, 128, 230 } : new byte[] { 52, 128, 230 });
            Func<byte, double> dec = b => b / 255.0 * 2.0 - 1.0;
            double sA = dec(204) / dec(230), sB = dec(52) / dec(230), sC = dec(128) / dec(230);
            double wantBoard = Math.Abs(sA - sB) / 2.0;
            int used, usedTilt, usedFlat, usedNone;
            double spread = LiquidBump.SlopeSpread(board, 64, 64, out used);
            double tiltSpread = LiquidBump.SlopeSpread(Map(32, 32, (x, y) => new byte[] { 204, 128, 230 }), 32, 32, out usedTilt);
            double flatSpread = LiquidBump.SlopeSpread(flat, 4, 4, out usedFlat);
            double noSpread = LiquidBump.SlopeSpread(black, 8, 8, out usedNone);
            Check(Near(spread, wantBoard, 1e-12) && used == 64 * 64 && tiltSpread < 1e-6 && usedTilt == 32 * 32 && flatSpread < 1e-6 && usedFlat == 16 &&
                  noSpread == 0.0 && usedNone == 0 && sC != 0,
                  "H7: the slope spread of a two-tilt checkerboard is half its slopes' difference (" + spread.ToString("0.0000") + "); a constant " +
                  "tilt and a flat map have none; texels that are not unit (all black) do not count");
            // A gentle map (the gentle one above, its slopes spread about 0.27) and a steep one, like the client's maps.
            double gentleSpread = LiquidBump.SlopeSpread(gentle, 512, 512, out used);
            Check(LiquidBump.TunedSlopeSpread == 0.0957 && LiquidBump.MaxStrengthFactor == 2.0 && LiquidBump.StrengthFactor(0.0957) == 1.0 &&
                  Near(LiquidBump.StrengthFactor(0.4635), 0.0957 / 0.4635, 1e-15) && Near(LiquidBump.StrengthFactor(0.0753), 0.0957 / 0.0753, 1e-15) &&
                  LiquidBump.StrengthFactor(0.03) == 2.0 && LiquidBump.StrengthFactor(0) == 1.0 && LiquidBump.StrengthFactor(double.NaN) == 1.0 &&
                  gentleSpread > 0.1 && gentleSpread < 0.4 && Near(LiquidBump.StrengthFactor(gentleSpread), 0.0957 / gentleSpread, 1e-15) &&
                  LiquidBump.StrengthFactor(spread) < 1.0,
                  "H7: the strength factor is the tuned map's spread (newoceanbump.blp, 0.0957) over the map's: 1 for it, 0.206 for a spread of " +
                  "0.4635 (oceanbump.blp's), 1.27 for 0.0753 (mercurybump.blp's), at most 2, and 1 for a flat map (gentle test map: spread " +
                  gentleSpread.ToString("0.0000") + ")");

            // The mean slope (the map's constant tilt, taken out in the shader before the strength scale): a checkerboard of two tilts
            // about a known mean -- the mean is the two slopes' average, the spread the same as the first overload's; RemoveTilt on
            // every texel leaves slopes of mean 0 and the same spread; a constant tilt leaves nothing; a flat map's mean is the
            // bytes' own 1 / 255; nothing counted: (0, 0).
            byte[] tilted = Map(64, 64, (x, y) => (x + y) % 2 == 0 ? new byte[] { 204, 128, 230 } : new byte[] { 89, 147, 247 });
            double tmx, tmy, cmx, cmy, fmx, fmy, nmx, nmy;
            int usedT, usedT2, usedC, usedF, usedN;
            double spreadT = LiquidBump.SlopeSpread(tilted, 64, 64, out usedT, out tmx, out tmy);
            double spreadT2 = LiquidBump.SlopeSpread(tilted, 64, 64, out usedT2);
            double wantMx = (dec(204) / dec(230) + dec(89) / dec(247)) / 2, wantMy = (dec(128) / dec(230) + dec(147) / dec(247)) / 2;
            double sX = 0, sY = 0, sXX = 0, sYY = 0;
            int nT = 0;
            for (int ty = 0; ty < 64; ty++)
                for (int tx = 0; tx < 64; tx++)
                {
                    int at = 4 * (ty * 64 + tx);
                    double nx = dec(tilted[at]), ny = dec(tilted[at + 1]), nz = dec(tilted[at + 2]);
                    LiquidHighMath.RemoveTilt(ref nx, ref ny, nz, tmx, tmy);
                    sX += nx / nz;
                    sY += ny / nz;
                    sXX += (nx / nz) * (nx / nz);
                    sYY += (ny / nz) * (ny / nz);
                    nT++;
                }
            double afterMx = sX / nT, afterMy = sY / nT;
            double afterSpread = Math.Sqrt(sXX / nT - afterMx * afterMx + sYY / nT - afterMy * afterMy);
            LiquidBump.SlopeSpread(Map(32, 32, (x, y) => new byte[] { 204, 128, 230 }), 32, 32, out usedC, out cmx, out cmy);
            double cx = dec(204), cy = dec(128), cz = dec(230);
            LiquidHighMath.RemoveTilt(ref cx, ref cy, cz, cmx, cmy);
            LiquidBump.SlopeSpread(flat, 4, 4, out usedF, out fmx, out fmy);
            LiquidBump.SlopeSpread(black, 8, 8, out usedN, out nmx, out nmy);
            Check(Near(tmx, wantMx, 1e-12) && Near(tmy, wantMy, 1e-12) && Math.Abs(wantMx) > 0.1 && Math.Abs(wantMy) > 0.05 && spreadT == spreadT2 &&
                  usedT == 64 * 64 && usedT2 == usedT && nT == usedT && Math.Abs(afterMx) < 1e-12 && Math.Abs(afterMy) < 1e-12 &&
                  Near(afterSpread, spreadT, 1e-9) && usedC == 32 * 32 && Math.Abs(cx) < 1e-12 && Math.Abs(cy) < 1e-12 && Near(fmx, 1 / 255.0, 1e-12) &&
                  Near(fmy, 1 / 255.0, 1e-12) && usedF == 16 && nmx == 0 && nmy == 0 && usedN == 0,
                  string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                "mean tilt: a two-tilt checkerboard's mean slope is its slopes' average ({0:0.0000}, {1:0.0000}); taken out of every " +
                                "texel (RemoveTilt) the slopes' mean is 0 ({2:0.0e+0}, {3:0.0e+0}) and their spread the same ({4:0.0000} -> {5:0.0000}); " +
                                "a constant tilt leaves nothing; a flat map's mean is the bytes' 1/255; no texel counted: (0, 0)",
                                tmx, tmy, afterMx, afterMy, spreadT, afterSpread));
        }

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();
            log.Add("High look: the formulas");
            FormulaTests();
            log.Add("High look: the tune");
            TuneTests();
            log.Add("High look: the ripple map's check");
            BumpTests();
            log.Add(string.Format("LiquidHighTests: {0} failure(s)", failures));
            if (output != null)
                foreach (string line in log)
                    output(line);
            return failures;
        }
    }
}
