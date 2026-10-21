// LiquidLightTests.cs
//
// Unit tests of the Stage 09 tuning's pure parts: the zone light the water and ocean colours follow
// (Wow/World/LiquidLights.cs -- the polygon test with both windings, the Z band, the zone order, the default light, an
// invalid base light, point lights weakest first and their weight ramp, the Durotar values at 12:00 from a small fixture
// of the client's own rows, the Add methods' validation, the ease and its snap rules; the High look's optional sky -- read,
// carried through the selection and eased), and the mesh inputs the shader's
// zone path reads (Wow/World/LiquidFormat.cs: the depth byte's W and O scales; Wow/World/TerrainLiquidMesh.cs: the planar
// uv on the game's axes at twice the repeats). Framework-free like the other suites: RunAll returns the failure count.

using System;
using System.Collections.Generic;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class LiquidLightTests
    {
        static int failures;
        static readonly List<string> log = new List<string>();

        static void Check(bool condition, string name)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name); failures++; }
        }

        static bool Near(double a, double b, double eps = 1e-9) { return Math.Abs(a - b) <= eps; }

        static float[] Rgb(float r, float g, float b) { return new[] { r, g, b }; }

        static float[] Pair(float s, float d) { return new[] { s, d }; }

        /// <summary>A light whose four colours are all (c, c, c) bytes and whose alphas are (a, a), (a, a).</summary>
        static void Flat(LiquidLights l, int light, float c, float a = 0.5f)
        {
            l.AddValues(light, light + 1000, Rgb(c, c, c), Rgb(c, c, c), Rgb(c, c, c), Rgb(c, c, c), Pair(a, a), Pair(a, a));
        }

        static double[] Square(double x0, double y0, double x1, double y1, bool clockwise)
        {
            return clockwise ? new[] { x0, y0, x0, y1, x1, y1, x1, y0 } : new[] { x0, y0, x1, y0, x1, y1, x0, y1 };
        }

        // ================================================================================== selection

        static void ContainmentTests()
        {
            var l = new LiquidLights();
            l.AddZone(1, 10, -100, 100, 0, Square(0, 0, 10, 10, false));
            l.AddZone(2, 20, -100, 100, 0, Square(20, 0, 30, 10, true));
            // An L: (0,0)-(10,0)-(10,4)-(4,4)-(4,10)-(0,10); (7, 7) lies in its notch, inside its bounding box.
            l.AddZone(3, 30, -100, 100, 0, new double[] { 40, 0, 50, 0, 50, 4, 44, 4, 44, 10, 40, 10 });
            LiquidLightZone a = l.Zones[0], b = l.Zones[1], c = l.Zones[2];
            Check(a.Contains(5, 5) && !a.Contains(15, 5) && !a.Contains(5, -1) && b.Contains(25, 5) && !b.Contains(19, 5) && !b.Contains(25, 11),
                  "containment: a counter-clockwise and a clockwise square both hold their inside and not their outside (even-odd: the winding is mixed)");
            Check(c.Contains(42, 7) && c.Contains(47, 2) && !c.Contains(47, 7) && c.MinX == 40 && c.MaxX == 50 && c.MinY == 0 && c.MaxY == 10,
                  "containment: a concave polygon -- a point in its notch is outside though inside its bounding box");
            double[] rep = { 0, 0, 10, 0, 10, 0, 10, 10, 0, 10 };
            l.AddZone(4, 40, -100, 100, 0, rep);
            Check(l.Zones[3].Contains(5, 5) && !l.Zones[3].Contains(11, 5), "containment: a repeated point (a zero-length edge, as in the client's rows) is harmless");
        }

        static void SelectionTests()
        {
            // The zone band: a zone between Z 0 and 100 over a whole-map zone; the default light 99.
            var l = new LiquidLights { Map = 7, DefaultLight = 99 };
            l.AddZone(1, 10, 0, 100, 0, Square(0, 0, 10, 10, false));
            l.AddZone(2, 20, -64000, 64000, 0, Square(-1000, -1000, 1000, 1000, true));
            Flat(l, 10, 10);
            Flat(l, 20, 20);
            Flat(l, 99, 99);
            LiquidLightSample s50 = l.Evaluate(5, 5, 50), s150 = l.Evaluate(5, 5, 150), sm1 = l.Evaluate(5, 5, -1), s0 = l.Evaluate(5, 5, 0),
                              s100 = l.Evaluate(5, 5, 100), far = l.Evaluate(5000, 5, 0);
            Check(s50.Valid && s50.Zone == 1 && s50.Light == 10 && s50.LightParams == 1010 && Near(s50.V[0], 10 / 255.0) && s0.Light == 10 && s100.Light == 10 &&
                  s150.Zone == 2 && s150.Light == 20 && sm1.Light == 20,
                  "Z band: zmin <= Z <= zmax (0 and 100 included); above or below it the next zone in order applies");
            Check(far.Valid && far.Zone == 0 && far.Light == 99 && Near(far.V[LiquidLightSample.OceanFar + 2], 99 / 255.0) && far.Why == "",
                  "default: outside every zone the map's default light (zone 0)");

            // Zone order: nested zones, the first in the sent order wins (the host sends the inner one first).
            var inner = new LiquidLights { DefaultLight = 0 };
            inner.AddZone(5, 50, -10, 10, 0, Square(2, 2, 4, 4, false));
            inner.AddZone(6, 60, -10, 10, 0, Square(0, 0, 10, 10, false));
            var outer = new LiquidLights { DefaultLight = 0 };
            outer.AddZone(6, 60, -10, 10, 0, Square(0, 0, 10, 10, false));
            outer.AddZone(5, 50, -10, 10, 0, Square(2, 2, 4, 4, false));
            foreach (LiquidLights x in new[] { inner, outer })
            {
                Flat(x, 50, 50);
                Flat(x, 60, 60);
            }
            Check(inner.Evaluate(3, 3, 0).Light == 50 && inner.Evaluate(8, 8, 0).Light == 60 && outer.Evaluate(3, 3, 0).Light == 60,
                  "zone order: the first zone in the sent order that holds the point wins (inner first: the inner light; outer first: the outer)");
            LiquidLightSample none = inner.Evaluate(50, 50, 0);
            Check(!none.Valid && none.Light == 0 && none.Zone == -1 && none.Why == LiquidLightSample.WhyNoLight,
                  "no zone and no default light: invalid (the type colours)");

            // An invalid base light: a zone whose light has no values; a point light that covers the spot is not applied.
            var bare = new LiquidLights { DefaultLight = 0 };
            bare.AddZone(8, 80, -10, 10, 0, Square(0, 0, 10, 10, false));
            bare.AddPoint(81, 5, 5, 0, 10, 20);
            Flat(bare, 81, 200);
            LiquidLightSample inv = bare.Evaluate(5, 5, 0);
            Check(!inv.Valid && inv.Light == 80 && inv.Zone == 8 && inv.Points == 0 && inv.Why == LiquidLightSample.WhyNoValues && inv.V[0] == 0,
                  "a base light without values is invalid: the type colours, and point lights are not applied");
            var defaultBare = new LiquidLights { DefaultLight = 77 };
            LiquidLightSample dinv = defaultBare.Evaluate(0, 0, 0);
            Check(!dinv.Valid && dinv.Zone == 0 && dinv.Light == 77 && dinv.Why == LiquidLightSample.WhyNoValues,
                  "a default light without values is invalid too");
        }

        static void PointTests()
        {
            var p = new LiquidLightPoint { Light = 1, Start = 10, End = 30 };
            Check(p.Weight(0) == 1 && p.Weight(10) == 1 && Near(p.Weight(20), 0.5) && Near(p.Weight(25), 0.25) && p.Weight(30) == 0 && p.Weight(31) == 0,
                  "weight ramp: 1 within start, (end - d) / (end - start) between, 0 at end and beyond");
            var q = new LiquidLightPoint { Light = 1, Start = 30, End = 30 };
            var r = new LiquidLightPoint { Light = 1, Start = 40, End = 30 };
            Check(q.Weight(29.9) == 1 && q.Weight(30) == 0 && r.Weight(0) == 1 && r.Weight(29) == 1 && r.Weight(30) == 0,
                  "weight ramp, end <= start: 1 wherever d < end");

            // Weakest first: from base 0, A (w 0.25, colour 1) then B (w 0.75, colour 0) gives 0.0625; the other order would give 0.25.
            var l = new LiquidLights { DefaultLight = 1 };
            Flat(l, 1, 0);
            l.AddPoint(11, 0, 25, 0, 0, 100);       // B: d 25, w 0.75 (added first)
            l.AddPoint(10, 75, 0, 0, 0, 100);       // A: d 75, w 0.25
            l.AddPoint(12, 0, 0, 0, 0, 100);        // no values: never applied
            l.AddPoint(13, 0, 0, 500, 0, 100);      // out of reach (3D distance 500)
            Flat(l, 10, 255);
            Flat(l, 11, 0);
            Flat(l, 13, 255);
            LiquidLightSample s = l.Evaluate(0, 0, 0);
            Check(s.Valid && s.Light == 1 && s.Points == 2 && Near(s.V[LiquidLightSample.RiverClose], 0.0625) && Near(s.V[LiquidLightSample.OceanFar + 1], 0.0625),
                  "point lights: applied weakest first, result = lerp(result, point, w) (0 -> 0.25 -> 0.0625); one without values and one out " +
                  "of reach in 3D are not applied; the base light stays the zone's");
            // Ties by light id: two at w 0.5, 20 (colour 1) before 21 (colour 0): 0.5 then 0.25 (added in the other order).
            var t = new LiquidLights { DefaultLight = 1 };
            Flat(t, 1, 0);
            t.AddPoint(21, 50, 0, 0, 0, 100);
            t.AddPoint(20, 0, 50, 0, 0, 100);
            Flat(t, 20, 255);
            Flat(t, 21, 0);
            Check(Near(t.Evaluate(0, 0, 0).V[0], 0.25), "point lights: equal weights in light id order (20 before 21: 0.25)");

            // Three overlapping, added strongest first and the tie in reverse id order, so the in-place insertion has to move
            // both: 30 (w 0.75, colour 1), 32 (w 0.5, colour 0), 31 (w 0.5, colour 1). By hand, weakest first and ties by id:
            // 0 -> 31: 0 + (1 - 0) x 0.5 = 0.5 -> 32: 0.5 + (0 - 0.5) x 0.5 = 0.25 -> 30: 0.25 + (1 - 0.25) x 0.75 = 0.8125.
            // The other five orders give 0.875 (32 31 30, 32 30 31), 0.4375 (30 31 32, 31 30 32) or 0.6875 (30 32 31).
            // The sample is reused across an evaluation that applies none, so the reused point-light list starts empty.
            var three = new LiquidLights { DefaultLight = 1 };
            Flat(three, 1, 0);
            three.AddPoint(30, 0, 25, 0, 0, 100);    // d 25: w 0.75
            three.AddPoint(32, 0, -50, 0, 0, 100);   // d 50: w 0.5
            three.AddPoint(31, 50, 0, 0, 0, 100);    // d 50: w 0.5
            Flat(three, 30, 255);
            Flat(three, 31, 255);
            Flat(three, 32, 0);
            var reused = new LiquidLightSample();
            three.Evaluate(0, 0, 0, reused);
            bool once = reused.Valid && reused.Light == 1 && reused.Points == 3 && Near(reused.V[LiquidLightSample.RiverClose], 0.8125) &&
                        Near(reused.V[LiquidLightSample.OceanFar + 2], 0.8125) && Near(reused.V[LiquidLightSample.OceanAlpha + 1], 0.5);
            three.Evaluate(1000, 0, 0, reused);
            bool away = reused.Valid && reused.Points == 0 && reused.V[LiquidLightSample.RiverClose] == 0;
            three.Evaluate(0, 0, 0, reused);
            bool again = reused.Points == 3 && Near(reused.V[LiquidLightSample.RiverClose], 0.8125);
            Check(once && away && again,
                  "point lights: three overlapping, added 30 (w 0.75), 32, 31 (both w 0.5), applied 31, 32, 30: 0 -> 0.5 -> 0.25 -> 0.8125 " +
                  "(every other order gives 0.875, 0.4375 or 0.6875); the same into a reused sample after a spot none reaches");
        }

        // ================================================================================== the Durotar fixture

        // Retail 12.1.5 map 1 (PROVEN_BYTES rows, as the host sends them): ZoneLight 140 "Durotar Org" (Light 239) inside 139
        // "Durotar" (Light 204) -- the smaller first --, the map's default Light 191, the point light 2677, and their slot-0
        // values at 12:00 (LightParams 6033, 5985, 192, 971).
        static readonly double[] DurotarOrg =
        {
            1756.58, -3881.71, 2302.22, -4060.17, 2347.35, -4484.79, 2539.15, -4761.71, 2519.66, -4923.76, 2415.04, -5034.53, 2304.27, -5385.3,
            2304.27, -5385.3, 2220.17, -5467.35, 1897.09, -4974.02, 1732.99, -4615.04, 1415.17, -4526.62, 1380.29, -4149.65, 1658.12, -3943.25,
        };

        static readonly double[] Durotar =
        {
            1422.2, -6166.98, 1259.74, -5997.53, 985.5, -5981.62, 392.04, -6014.36, 73.59, -6100.55, -400.15, -6313.25, -895.59, -6286.25,
            -1382.03, -6371.38, -2056.5, -6139.51, -2375.46, -5962.71, -1824.82, -4489.32, -1252.51, -4107.79, -729.43, -4003.17, 39.8, -3732.4,
            938.26, -3769.32, 1418.26, -3849.32, 1855.18, -3781.63, 2316.72, -3849.32, 2482.88, -4169.32, 2575.18, -4581.63, 2550.57, -5012.4,
            2384.41, -5412.4, 2107.49, -5787.79,
        };

        static LiquidLights DurotarFixture()
        {
            var l = new LiquidLights { Map = 1, Time = 1440, Slot = 0, DefaultLight = 191, DefaultFrom = "end0", Product = "wow", Build = "12.1.5.70448" };
            l.AddZone(140, 239, -64000, 64000, 0, DurotarOrg);
            l.AddZone(139, 204, -64000, 64000, 0, Durotar);
            l.AddPoint(2677, 1547.22, -4162.36, 47.75, 60.4, 76.79);
            l.AddValues(204, 5985, Rgb(64, 62, 35), Rgb(62, 54, 47), Rgb(82, 90, 94), Rgb(55, 61, 69), Pair(0.4f, 0.8f), Pair(0.75f, 1f));
            l.AddValues(239, 6033, Rgb(90, 59, 52), Rgb(58, 58, 51), Rgb(114, 92, 79), Rgb(86, 56, 97), Pair(0.5f, 0.8f), Pair(0.75f, 1f));
            l.AddValues(191, 192, Rgb(88, 84, 69), Rgb(43, 67, 81), Rgb(16, 65, 77), Rgb(0, 29, 41), Pair(0.5f, 1f), Pair(0.75f, 1f));
            l.AddValues(2677, 971, Rgb(42, 64, 45), Rgb(55, 83, 94), Rgb(114, 92, 79), Rgb(86, 56, 97), Pair(0.5f, 0.8f), Pair(0.75f, 1f));
            return l;
        }

        static bool Is(LiquidLightSample s, int at, double r, double g, double b)
        {
            return Near(s.V[at], r / 255.0) && Near(s.V[at + 1], g / 255.0) && Near(s.V[at + 2], b / 255.0);
        }

        static void DurotarTests()
        {
            LiquidLights l = DurotarFixture();
            // Southfury River, the references' spot (X 1261, Y -3948; water at 17.88).
            LiquidLightSample s = l.Evaluate(1261, -3948, 17.88);
            Check(s.Valid && s.Zone == 139 && s.Light == 204 && s.LightParams == 5985 && s.Points == 0 && Is(s, LiquidLightSample.RiverClose, 64, 62, 35) &&
                  Is(s, LiquidLightSample.RiverFar, 62, 54, 47) && Is(s, LiquidLightSample.OceanClose, 82, 90, 94) && Is(s, LiquidLightSample.OceanFar, 55, 61, 69) &&
                  Near(s.V[LiquidLightSample.WaterAlpha], 0.4, 1e-6) && Near(s.V[LiquidLightSample.WaterAlpha + 1], 0.8, 1e-6) &&
                  Near(s.V[LiquidLightSample.OceanAlpha], 0.75, 1e-6) && Near(s.V[LiquidLightSample.OceanAlpha + 1], 1.0, 1e-6),
                  "Durotar at 12:00 (X 1261, Y -3948): zone 139 -> light 204, params 5985: river (64,62,35)/(62,54,47), ocean (82,90,94)/(55,61,69), " +
                  "water alpha 0.40/0.80, ocean 0.75/1.00; the nearest point light (2677, 361 yd) does not reach");
            LiquidLightSample org = l.Evaluate(1900, -4400, 20);
            Check(org.Valid && org.Zone == 140 && org.Light == 239 && org.LightParams == 6033 && Is(org, LiquidLightSample.RiverClose, 90, 59, 52),
                  "Durotar Org, inside Durotar and sent before it: light 239 (90,59,52)");
            LiquidLightSample map = l.Evaluate(0, 0, 0);
            Check(map.Valid && map.Zone == 0 && map.Light == 191 && map.LightParams == 192 && Is(map, LiquidLightSample.RiverFar, 43, 67, 81),
                  "outside both: Kalimdor's default light 191 (river far (43,67,81))");
            LiquidLightSample at = l.Evaluate(1547.22, -4162.36, 47.75);
            LiquidLightSample half = l.Evaluate(1547.22 + (60.4 + 76.79) / 2, -4162.36, 47.75);
            Check(at.Valid && at.Light == 239 && at.Points == 1 && Is(at, LiquidLightSample.RiverClose, 42, 64, 45) && half.Points == 1 &&
                  Is(half, LiquidLightSample.RiverClose, 66, 61.5, 48.5),
                  "point light 2677 inside Durotar Org: its own values at its position (w 1), half way along its ramp the mean with light 239's " +
                  "((90,59,52) + (42,64,45)) / 2; the base light stays 239");
            Check(l.Summary().Contains("2 zone(s), 1 point light(s), 4 light(s) with values, default light 191 (from end0)"), "summary: " + l.Summary());
        }

        static void ValidationTests()
        {
            var l = new LiquidLights();
            bool two = l.AddZone(1, 1, 0, 1, 0, new double[] { 0, 0, 1, 1 });
            bool odd = l.AddZone(2, 1, 0, 1, 0, new double[] { 0, 0, 1, 0, 1, 1, 0 });
            bool nul = l.AddZone(3, 1, 0, 1, 0, null);
            bool nan = l.AddZone(4, 1, double.NaN, 1, 0, Square(0, 0, 1, 1, false));
            bool ok = l.AddZone(5, 1, 0, 1, 0, new double[] { 0, 0, 1, 0, 1, 1 });
            Check(!two && !odd && !nul && !nan && ok && l.Zones.Count == 1 && l.DroppedZones == 4 && l.Zones[0].PointCount == 3,
                  "zones: fewer than 3 points, an odd coordinate count, none, or a value not finite are dropped and counted; a triangle is kept");
            bool shortColour = l.AddValues(1, 2, Rgb(1, 2, 3), new float[] { 1, 2 }, Rgb(1, 2, 3), Rgb(1, 2, 3), Pair(0, 1), Pair(0, 1));
            bool shortAlpha = l.AddValues(1, 2, Rgb(1, 2, 3), Rgb(1, 2, 3), Rgb(1, 2, 3), Rgb(1, 2, 3), new float[] { 0.5f }, Pair(0, 1));
            bool nullAlpha = l.AddValues(1, 2, Rgb(1, 2, 3), Rgb(1, 2, 3), Rgb(1, 2, 3), Rgb(1, 2, 3), Pair(0, 1), null);
            bool good = l.AddValues(1, 2, Rgb(1, 2, 3), Rgb(4, 5, 6), Rgb(7, 8, 9), Rgb(10, 11, 12), Pair(0.25f, 0.5f), Pair(0.75f, 1f));
            bool again = l.AddValues(1, 3, Rgb(255, 255, 255), Rgb(4, 5, 6), Rgb(7, 8, 9), Rgb(10, 11, 12), Pair(0.25f, 0.5f), Pair(0.75f, 1f));
            LiquidLightValues v;
            Check(!shortColour && !shortAlpha && !nullAlpha && good && again && l.DroppedValues == 3 && l.ValueCount == 1 && l.TryGetValues(1, out v) &&
                  v.LightParams == 3 && v.V[0] == 1.0 && Near(v.V[LiquidLightSample.OceanFar + 2], 12 / 255.0) && v.V[LiquidLightSample.WaterAlpha] == 0.25 &&
                  v.V[LiquidLightSample.OceanAlpha + 1] == 1.0,
                  "values: a colour short of 3 or an alpha pair short of 2 (or missing) drops the row; a later row for a light replaces it; " +
                  "colours / 255, alphas as given");
            bool badPoint = l.AddPoint(9, 0, double.PositiveInfinity, 0, 1, 2);
            Check(!badPoint && l.DroppedPoints == 1 && l.Points.Count == 0, "points: a value not finite drops the point");
        }

        // ================================================================================== the ease

        static LiquidLightSample Sample(bool valid, double v)
        {
            var s = new LiquidLightSample { Valid = valid, Light = valid ? 1 : 0, Zone = 1 };
            for (int k = 0; k < LiquidLightSample.Count; k++) s.V[k] = v;
            return s;
        }

        static void EaseTests()
        {
            Check(Near(LiquidLightEase.Factor(0.25), 1 - Math.Exp(-1)) && Near(LiquidLightEase.Factor(1.0 / 60), 1 - Math.Exp(-1.0 / 15)) &&
                  LiquidLightEase.Factor(0) == 0 && LiquidLightEase.Factor(-1) == 0 && LiquidLightEase.Factor(double.NaN) == 0 &&
                  LiquidLightEase.TimeConstant == 0.25 && LiquidLightEase.SnapDistance == 60,
                  "ease factor 1 - exp(-dt / 0.25 s) (0.632 at 0.25 s, 0.0645 at 60 fps); none for no time");
            object a = new object(), b = new object();
            var e = new LiquidLightEase();
            string r1 = e.Step(Sample(true, 0.2), a, 0, 0, 0, 0.016, false);
            bool first = r1 == LiquidLightEase.SnapFirst && e.Valid && e.V[0] == 0.2 && e.V[15] == 0.2 && LiquidLightSample.Count == 19 &&
                         e.V[LiquidLightSample.Count - 1] == 0.2 && !e.HasSky;
            string r2 = e.Step(Sample(true, 0.6), a, 10, 0, 0, 0.25, false);
            bool eased = r2 == null && Near(e.V[0], 0.2 + 0.4 * (1 - Math.Exp(-1))) && Near(e.V[7], e.V[0]);
            string r3 = e.Step(Sample(true, 0.6), a, 70, 0, 0, 0.016, false);       // 60 yd from the last sample: still eased
            bool at60 = r3 == null && e.V[0] < 0.6;
            string r4 = e.Step(Sample(true, 0.6), a, 70, 60.01, 0, 0.016, false);  // 60.01 yd: a jump
            bool jump = r4 == LiquidLightEase.SnapJump && e.V[0] == 0.6;
            Check(first && eased && at60 && jump,
                  "ease: the first sample snaps; then each frame moves by the factor (0.2 -> 0.2 + 0.4 x 0.632 in 0.25 s); a move of exactly 60 yd " +
                  "still eases, of more snaps (" + r1 + ", " + r2 + ", " + r3 + ", " + r4 + ")");
            string r5 = e.Step(Sample(true, 0.1), b, 70, 60.01, 0, 0.016, false);
            bool lights = r5 == LiquidLightEase.SnapLights && e.V[0] == 0.1;
            string r6 = e.Step(Sample(false, 0.9), b, 70, 60.01, 0, 0.016, false);
            bool invalid = r6 == LiquidLightEase.Invalid && !e.Valid && e.V[0] == 0.1;
            string r7 = e.Step(Sample(true, 0.3), b, 70, 60.01, 0, 0.016, false);
            bool validity = r7 == LiquidLightEase.SnapValidity && e.Valid && e.V[0] == 0.3;
            string r8 = e.Step(Sample(true, 0.5), b, 70, 60.01, 0, 0.016, true);
            bool pinned = r8 == LiquidLightEase.SnapPinned && e.V[0] == 0.5;
            string r9 = e.Step(Sample(true, 0.5), b, 70, 60.01, 0, 0.0, false);
            e.Reset();
            string r10 = e.Step(Sample(true, 0.7), b, 70, 60.01, 0, 0.016, false);
            Check(lights && invalid && validity && pinned && r9 == null && e.V[0] == 0.7 && r10 == LiquidLightEase.SnapFirst,
                  "ease snaps: another lights object, an invalid sample (off at once, values kept), valid again, the clock pinned (every frame); " +
                  "no time, no move; after Reset the first sample snaps (" + r5 + ", " + r6 + ", " + r7 + ", " + r8 + ", " + (r9 ?? "eased") + ", " + r10 + ")");
        }

        // ================================================================================== the sky (the High look)

        static void SkyTests()
        {
            var l = new LiquidLights { Map = 1, DefaultLight = 191 };
            bool withSky = l.AddValues(191, 192, Rgb(88, 84, 69), Rgb(43, 67, 81), Rgb(16, 65, 77), Rgb(0, 29, 41), Pair(0.5f, 1f), Pair(0.75f, 1f),
                                       Rgb(102, 153, 204));
            bool noSky = l.AddValues(9, 10, Rgb(1, 2, 3), Rgb(1, 2, 3), Rgb(1, 2, 3), Rgb(1, 2, 3), Pair(0.5f, 0.5f), Pair(0.5f, 0.5f), null);
            bool emptySky = l.AddValues(11, 12, Rgb(1, 2, 3), Rgb(1, 2, 3), Rgb(1, 2, 3), Rgb(1, 2, 3), Pair(0.5f, 0.5f), Pair(0.5f, 0.5f), new float[0]);
            bool shortSky = l.AddValues(13, 14, Rgb(1, 2, 3), Rgb(1, 2, 3), Rgb(1, 2, 3), Rgb(1, 2, 3), Pair(0.5f, 0.5f), Pair(0.5f, 0.5f),
                                        new float[] { 9, 9 });
            bool nanSky = l.AddValues(15, 16, Rgb(1, 2, 3), Rgb(1, 2, 3), Rgb(1, 2, 3), Rgb(1, 2, 3), Pair(0.5f, 0.5f), Pair(0.5f, 0.5f),
                                      Rgb(9, float.NaN, 9));
            bool zeroSky = l.AddValues(21, 22, Rgb(0, 0, 0), Rgb(0, 0, 0), Rgb(0, 0, 0), Rgb(0, 0, 0), Pair(0f, 0f), Pair(0f, 0f), Rgb(0, 0, 0));
            LiquidLightValues v191 = null, v9 = null, v11 = null, v13 = null, v15 = null;
            bool got = l.TryGetValues(191, out v191) && l.TryGetValues(9, out v9) && l.TryGetValues(11, out v11) && l.TryGetValues(13, out v13) &&
                       l.TryGetValues(15, out v15);
            Check(withSky && noSky && emptySky && shortSky && nanSky && zeroSky && got && l.DroppedValues == 0 && l.ValueCount == 6 && v191.HasSky &&
                  Near(v191.V[LiquidLightSample.Sky], 0.4) && Near(v191.V[LiquidLightSample.Sky + 1], 0.6) && Near(v191.V[LiquidLightSample.Sky + 2], 0.8) &&
                  !v9.HasSky && !v11.HasSky && !v13.HasSky && !v15.HasSky && v15.V[LiquidLightSample.Sky] == 0 && v15.V[LiquidLightSample.Sky + 2] == 0 &&
                  LiquidLightSample.Sky == 16,
                  "sky: the optional sky is bytes / 255 ((102, 153, 204) -> 0.4, 0.6, 0.8); absent, empty (an older host's row), short or not " +
                  "finite: no sky, the row kept");

            // The selection: the base light's sky; a point light that carries a sky moves it by its weight, one without leaves it.
            l.AddPoint(21, 10, 0, 0, 0, 20);                         // at the origin: d 10 of 0..20 -> weight 0.5, sky (0, 0, 0)
            l.AddPoint(9, -10, 0, 0, 50, 100);                       // d 10 within its start -> weight 1, no sky (applied last)
            LiquidLightSample s = l.Evaluate(0, 0, 0);
            Check(s.Valid && s.Light == 191 && s.Points == 2 && s.HasSky && Near(s.V[LiquidLightSample.Sky], 0.2) && Near(s.V[LiquidLightSample.Sky + 1], 0.3) &&
                  Near(s.V[LiquidLightSample.Sky + 2], 0.4) && Near(s.V[LiquidLightSample.RiverClose], 1 / 255.0) && Near(s.V[LiquidLightSample.OceanFar + 2], 3 / 255.0),
                  "sky: the base light's sky (0.4, 0.6, 0.8), halved by a point light of sky 0 at weight 0.5 -> (0.2, 0.3, 0.4); the stronger point light " +
                  "without a sky takes the colours (1, 2, 3) but leaves the sky");
            var m = new LiquidLights { Map = 1, DefaultLight = 9 };
            m.AddValues(9, 10, Rgb(1, 2, 3), Rgb(1, 2, 3), Rgb(1, 2, 3), Rgb(1, 2, 3), Pair(0.5f, 0.5f), Pair(0.5f, 0.5f));
            m.AddValues(21, 22, Rgb(0, 0, 0), Rgb(0, 0, 0), Rgb(0, 0, 0), Rgb(0, 0, 0), Pair(0f, 0f), Pair(0f, 0f), Rgb(255, 255, 255));
            m.AddPoint(21, 0, 0, 0, 50, 100);
            LiquidLightSample n = m.Evaluate(0, 0, 0);
            Check(n.Valid && n.Points == 1 && !n.HasSky, "sky: a base light without a sky gives none, whatever its point lights carry (the neutral sky)");

            // The ease: the sky eases with the rest; a sample that gains or loses its sky snaps (as a change of validity).
            var e = new LiquidLightEase();
            object src = new object();
            LiquidLightSample a = Sample(true, 0.2), b = Sample(true, 0.6), c = Sample(true, 0.6);
            a.HasSky = b.HasSky = true;
            string r1 = e.Step(a, src, 0, 0, 0, 0.016, false);
            string r2 = e.Step(b, src, 0, 0, 0, 0.25, false);
            bool eased = r1 == LiquidLightEase.SnapFirst && r2 == null && e.HasSky && Near(e.V[LiquidLightSample.Sky + 2], 0.2 + 0.4 * (1 - Math.Exp(-1)));
            string r3 = e.Step(c, src, 0, 0, 0, 0.016, false);
            bool lost = r3 == LiquidLightEase.SnapValidity && !e.HasSky && e.V[LiquidLightSample.Sky] == 0.6;
            string r4 = e.Step(b, src, 0, 0, 0, 0.016, false);
            bool gained = r4 == LiquidLightEase.SnapValidity && e.HasSky;
            string r5 = e.Step(Sample(false, 0.1), src, 0, 0, 0, 0.016, false);
            Check(eased && lost && gained && r5 == LiquidLightEase.Invalid && !e.HasSky,
                  "sky: eased like the colours (0.2 -> 0.2 + 0.4 x 0.632 in 0.25 s); a sample without a sky after one with it (and back) snaps; an " +
                  "invalid sample has none (" + r1 + ", " + (r2 ?? "eased") + ", " + r3 + ", " + r4 + ", " + r5 + ")");
        }

        // ================================================================================== mesh inputs

        static void MeshInputTests()
        {
            Check(LiquidFormat.DepthYardsW(0) == 0.5 / 9 && LiquidFormat.DepthYardsW(1) == 1.5 / 9 && LiquidFormat.DepthYardsW(14) == 14.5 / 9 &&
                  LiquidFormat.DepthYardsW(255) == 255.5 / 9,
                  "depth, W scale: (b + 0.5) / 9 yd -- 0 -> 0.0556, 14 -> 1.611, 255 -> 28.39");
            Check(LiquidFormat.DepthYardsO(0) == 0 && Near(LiquidFormat.DepthYardsO(1), 0.5 * 7 / 12) && Near(LiquidFormat.DepthYardsO(255), 254.5 * 7 / 12),
                  "depth, O scale: 0 for byte 0, else (b - 0.5) x 7/12 yd -- 1 -> 0.2917, 255 -> 148.46");
            bool round = true;
            for (int b = 0; b < 255; b++)
            {
                round &= Math.Min(255, (int)Math.Floor(9 * LiquidFormat.DepthYardsW((byte)b))) == b;
                round &= Math.Min(255, (int)Math.Ceiling(12.0 / 7 * LiquidFormat.DepthYardsO((byte)b) - 1e-9)) == b;
            }
            Check(round, "depth: every byte decodes to a depth its own scale's encoder (floor(9 D); ceil(12/7 D)) gives back");
            Check(LiquidFormat.DepthScaleO(2) && LiquidFormat.DepthScaleO(42) && !LiquidFormat.DepthScaleO(0) && !LiquidFormat.DepthScaleO(1) &&
                  !LiquidFormat.DepthScaleO(43) && !LiquidFormat.DepthScaleO(5987) && LiquidFormat.DepthYards(14, 42) == LiquidFormat.DepthYardsO(14) &&
                  LiquidFormat.DepthYards(14, 600) == LiquidFormat.DepthYardsW(14),
                  "depth scale by the instance's second word: 2 and 42 the O scale; 0, 1 and LiquidObject ids the W scale");
            double au, av, bu, bv, cu, cv;
            TerrainLiquidMesh.PlanarUv(1261, -3948, 1, 0, out au, out av);
            TerrainLiquidMesh.PlanarUv(1261, -3948, 1, 90, out bu, out bv);
            TerrainLiquidMesh.PlanarUv(1261, -3948, 0.5, 0, out cu, out cv);
            Check(Near(au, 75.66) && Near(av, -236.88) && Near(bu, 236.88) && Near(bv, 75.66) && Near(cu, 37.83) && Near(cv, -118.44) &&
                  TerrainLiquidMesh.PlanarRepeatFactor == 2.0,
                  "PlanarUv: game point (X 1261, Y -3948), Float0 1 -> (75.66, -236.88) = (X, Y) x 2 / C; Float1 90 -> (236.88, 75.66); Float0 0.5 -> " +
                  "(37.83, -118.44)");
        }

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();
            log.Add("Zone light: containment");
            ContainmentTests();
            log.Add("Zone light: selection");
            SelectionTests();
            log.Add("Zone light: point lights");
            PointTests();
            log.Add("Zone light: the Durotar fixture");
            DurotarTests();
            log.Add("Zone light: validation");
            ValidationTests();
            log.Add("Zone light: the ease");
            EaseTests();
            log.Add("Zone light: the sky (the High look)");
            SkyTests();
            log.Add("Mesh inputs: depth in yards, planar uv");
            MeshInputTests();
            log.Add(string.Format("LiquidLightTests: {0} failure(s)", failures));
            if (output != null)
                foreach (string line in log)
                    output(line);
            return failures;
        }
    }
}
