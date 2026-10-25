// WorldLightTests.cs
//
// Unit tests of Stage 10's outdoor light state (docs/world-viewer/10-light-sky-fog.md 6.4; step 3): the shared zone and point
// geometry (Wow/World/LightGeometry.cs), the host's "worldLight" object as the player holds it (Wow/World/WorldLight.cs: the
// column map, the day -- the half-minute, the host's rounding, wrap and the clamp arm, single keys, floats linear, integers
// from the earlier key --, the selection with Stage 09's rules and the debug arms, the slot fallback, invalid lights), the
// clock (the half-minute from HH:MM:SS, running and wrapping, the map's override), the sample and its channel groups, the
// ease, the switches (steps 3-7) and the graded hypothesis tables (Wow/World/WorldLightState.cs, WorldLightModel.cs; the fog's
// rows and the derived fog channels in detail: WorldFogTests.cs). A small fixture of
// the client's own rows (Retail 12.1.5 LightParams 5985, the Southfury stand's day) checks the values the plan names.
//
// With WMV_WORLD_LIGHT_FIXTURES naming the folder the offline generator wrote (<client>_map<m>.json in the spec's
// "worldLight" format, parity_<client>_map<m>.json, the survey's spots.json, and manifest.json, whose SHA-256 is pinned here
// and which pins every file's; D:\wmv-10\step3\player\scripts\gen_fixtures.py) it also checks, on maps 0 and 1 of the five
// clients: byte parity of every light's slot-0 day with the generator's OFFLINE PORT of the host's LightCatalog::sample()
// (not the host itself: the host's own values are the host-dump part) for the five colours it samples (1440 among the
// times), and the spot selections and day values of the survey's dumps. Without it that part reports SKIPPED. With
// WMV_WORLD_LIGHT_HOST_DUMPS naming a folder of the host's own objects (*.json, the object itself or a load line carrying it)
// every one is read through the same path and checked against the host's own Stage 09 sample() rows. Also: copy rows (two
// LightParams naming one day), and the point-light order arms on a case where all three orders differ.
// Framework-free like the other suites: RunAll returns the failure count.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text.Json;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class WorldLightTests
    {
        static int failures;
        static readonly List<string> log = new List<string>();

        static void Check(bool condition, string name)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name); failures++; }
        }

        static bool Near(double a, double b, double eps = 1e-9) { return Math.Abs(a - b) <= eps; }

        static double[] Square(double x0, double y0, double x1, double y1)
        {
            return new[] { x0, y0, x1, y0, x1, y1, x0, y1 };
        }

        // ================================================================================== the Southfury day (Retail LightParams 5985)

        // Retail 12.1.5 LightData of LightParams 5985 (PROVEN_BYTES: the client's rows), eight colour columns and three floats in
        // an order of the test's own (the map must put them where the player keeps them); integers all 0.
        static readonly string[] SfColours = { "AmbientColor", "SkyMiddleColor", "DirectColor", "SkyBand1Color", "RiverCloseColor", "RiverFarColor",
                                               "OceanCloseColor", "OceanFarColor" };
        static readonly string[] SfFloats = { "FogDensity", "FogScaler", "FogEnd" };
        static readonly int[] SfTimes = { 0, 480, 720, 1440, 2340, 2520, 2640 };
        static readonly int[] SfC =
        {
            0x3c5976, 0x232250, 0x24308e, 0x4c466e, 0x54442d, 0x0c1b25, 0x334c51, 0x1d3f69,
            0x3c5976, 0x232250, 0x24308e, 0x4c466e, 0x54442d, 0x0c1b25, 0x334c51, 0x1d3f69,
            0x989b9b, 0xedac68, 0x785400, 0xe59449, 0x4f422a, 0x545454, 0x596490, 0x364456,
            0xb7a5b8, 0xffe2af, 0x5e3200, 0xf6b258, 0x403e23, 0x3e362f, 0x525a5e, 0x373d45,
            0x7f6575, 0xcc947f, 0x7b3f00, 0xfa9c5f, 0x4c4129, 0x464956, 0x677ac2, 0x495eb2,
            0x694f5e, 0x583127, 0x3f3021, 0x985129, 0x4e422a, 0x484e5f, 0x60698d, 0x4e5984,
            0x416291, 0x2b2a62, 0x4b3453, 0x4c466e, 0x50422b, 0x323b42, 0x334c51, 0x364658,
        };
        static readonly double[] SfF =
        {
            3.0, 0.15000000596046448, 0.0, 3.0, 0.15000000596046448, 0.0, 2.5, 0.15000000596046448, 0.0, 3.0, 0.15000000596046448, 0.0,
            2.0, 0.15000000596046448, 0.0, 3.0, 0.15000000596046448, 0.0, 3.0, 0.15000000596046448, 0.0,
        };

        /// <summary>Map 1 as a small object: zone 139 (a square around the Southfury stand) -> Light 204 (slots 5985, 5987), the
        /// default Light 191 whose LightParams has no keys, the point Light 300 (its own day: every colour 255) at (1300, -3948,
        /// 20) reaching 100 yd.</summary>
        static WorldLightSet Southfury()
        {
            var s = new WorldLightSet { State = "ok", Product = "wow", Build = "12.1.5.70448", Map = 1, Branch = "retail", LightDataLayout = "6ABF2921",
                                        DefaultLight = 191, DefaultFrom = "end0" };
            s.SetColumns(SfColours, SfFloats, new[] { "ColorGradingFileDataID", "DarkerColorGradingFileDataID" }, new string[0]);
            int day = s.AddDay(SfTimes, SfC, SfF, new long[SfTimes.Length * 2]);
            var white = new int[8];
            for (int k = 0; k < 8; k++) white[k] = 0xFFFFFF;
            int whiteDay = s.AddDay(new[] { 0 }, white, new[] { 9.0, 9.0, 9.0 }, new long[] { 77, 78 });
            s.AddParams(5985, day, 0.0, 0, 0, 0, new double[3], 0, 0);
            s.AddParams(5987, -1, 0.0, 0, 0, 0, new double[3], 0, 0);
            s.AddParams(192, -1, 0.5, 0, 0, 0, new double[3], 0, 0);
            s.AddParams(301, whiteDay, 1.0, 0, 0, 0, new double[3], 0, 0);
            s.AddLight(204, new[] { 5985, 5987, 0, 0, 3, 0, 0, 0 });
            s.AddLight(191, new[] { 192, 0, 0, 0, 3, 0, 0, 0 });
            s.AddLight(300, new[] { 301, 0, 0, 0, 0, 0, 0, 0 });
            s.AddZone(139, 204, 0, -64000, 64000, 0, Square(1000, -4200, 1500, -3700));
            s.AddPoint(300, 1300, -3948, 20, 50, 100);
            return s;
        }

        static bool Rgb(int[] rgb, int column, int r, int g, int b)
        {
            return rgb[3 * column] == r && rgb[3 * column + 1] == g && rgb[3 * column + 2] == b;
        }

        static void DayTests()
        {
            WorldLightSet s = Southfury();
            WorldLightDay d = s.DayAt(0);
            var rgb = new int[3 * WorldLightColumns.ColourCount];
            var fl = new double[WorldLightColumns.FloatCount];
            var ints = new long[WorldLightColumns.IntCount];
            d.Evaluate(1440, false, rgb, fl, ints);
            Check(d.Keys == 7 && Rgb(rgb, WorldLightColumns.SkyMiddleColor, 255, 226, 175) && Rgb(rgb, WorldLightColumns.DirectColor, 94, 50, 0) &&
                  Rgb(rgb, WorldLightColumns.AmbientColor, 183, 165, 184) && Rgb(rgb, WorldLightColumns.RiverCloseColor, 64, 62, 35) &&
                  Rgb(rgb, WorldLightColumns.RiverFarColor, 62, 54, 47) && Rgb(rgb, WorldLightColumns.OceanCloseColor, 82, 90, 94) &&
                  Rgb(rgb, WorldLightColumns.OceanFarColor, 55, 61, 69) && Rgb(rgb, WorldLightColumns.SkyBand1Color, 246, 178, 88) &&
                  Rgb(rgb, WorldLightColumns.SkyTopColor, 0, 0, 0) && fl[WorldLightColumns.FogDensity] == 3.0 &&
                  fl[WorldLightColumns.FogScaler] == 0.15000000596046448 && fl[WorldLightColumns.FogEnd] == 0.0,
                  "Southfury day (Retail LightParams 5985) at 12:00, the key itself: SkyMiddle (255,226,175) and Direct (94,50,0) as the plan names " +
                  "them, Stage 09's water (64,62,35)/(62,54,47)/(82,90,94)/(55,61,69) and sky (246,178,88); the sent column order mapped; a column " +
                  "not sent stays 0");
            d.Evaluate(2160, false, rgb, fl, ints);
            bool six = Rgb(rgb, WorldLightColumns.DirectColor, 117, 60, 0) && Near(fl[WorldLightColumns.FogDensity], 2.2, 1e-15) &&
                       Rgb(rgb, WorldLightColumns.SkyMiddleColor, 214, 164, 137);
            d.Evaluate(720, false, rgb, fl, ints);
            bool am = Rgb(rgb, WorldLightColumns.DirectColor, 120, 84, 0) && Rgb(rgb, WorldLightColumns.SkyMiddleColor, 237, 172, 104) &&
                      fl[WorldLightColumns.FogDensity] == 2.5;
            d.Evaluate(0, false, rgb, fl, ints);
            bool night = Rgb(rgb, WorldLightColumns.DirectColor, 36, 48, 142) && Rgb(rgb, WorldLightColumns.AmbientColor, 60, 89, 118);
            Check(six && am && night,
                  "Southfury day at 18:00 (between 12:00 and 19:30, f 0.8): Direct (117,60,0), SkyMiddle (214,164,137), FogDensity 3 + (2 - 3) 0.8 = 2.2; " +
                  "06:00 and 00:00 the keys -- the survey's dump values (spots.json)");

            // The host's rounding by hand: a two-key day, 0 at t 0 and (255, 10, 1) at t 1440.
            var two = new WorldLightSet();
            two.SetColumns(new[] { "DirectColor" }, new[] { "FogEnd" }, new[] { "ColorGradingFileDataID" }, new string[0]);
            two.AddDay(new[] { 0, 1440 }, new[] { 0x000000, 0xFF0A01 }, new[] { 0.0, 1000.0 }, new long[] { 5, 6 });
            WorldLightDay t2 = two.DayAt(0);
            int r, g, b;
            t2.Colour(720, WorldLightColumns.DirectColor, false, out r, out g, out b);
            bool half = r == 128 && g == 5 && b == 1;
            t2.Colour(2160, WorldLightColumns.DirectColor, false, out r, out g, out b);
            bool wrapped = r == 128 && g == 5 && b == 1;
            t2.Colour(1, WorldLightColumns.DirectColor, false, out r, out g, out b);
            bool first = r == 0 && g == 0 && b == 0;           // 255 / 1440 + 0.5 = 0.68: 0
            t2.Colour(3, WorldLightColumns.DirectColor, false, out r, out g, out b);
            bool third = r == 1 && g == 0 && b == 0;           // 255 x 3 / 1440 + 0.5 = 1.03: 1
            t2.Evaluate(1000, false, rgb, fl, ints);
            bool linear = Near(fl[WorldLightColumns.FogEnd], 1000.0 * 1000 / 1440, 1e-12) && ints[0] == 5;
            t2.Evaluate(2000, false, rgb, fl, ints);
            bool across = Near(fl[WorldLightColumns.FogEnd], 1000.0 + (0.0 - 1000.0) * (560.0 / 1440), 1e-12) && ints[0] == 6;
            Check(half && wrapped && first && third && linear && across,
                  "the host's rounding floor(a + (b - a) f + 0.5) per channel: (127.5, 5, 0.5) + 0.5 -> (128, 5, 1) at 720; the same at 2160 across " +
                  "midnight; floats linear (FogEnd 694.4 at 1000, 611.1 at 2000 across midnight); integers the earlier key's (5, then 6)");

            // Wrap against the clamp arm: keys at 600 and 1800 only.
            var mid = new WorldLightSet();
            mid.SetColumns(new[] { "DirectColor" }, new string[0], new string[0], new string[0]);
            mid.AddDay(new[] { 600, 1800 }, new[] { 0x646464, 0xC8C8C8 }, new double[0], new long[0]);
            WorldLightDay m = mid.DayAt(0);
            m.Colour(0, WorldLightColumns.DirectColor, false, out r, out g, out b);      // 1800 -> 600 + 2880: f = 1080 / 1680
            int want = (int)Math.Floor(200 + (100 - 200) * (1080.0 / 1680) + 0.5);
            bool wrap0 = r == want;
            m.Colour(0, WorldLightColumns.DirectColor, true, out r, out g, out b);
            bool clamp0 = r == 100;
            m.Colour(2879, WorldLightColumns.DirectColor, true, out r, out g, out b);
            bool clampEnd = r == 200;
            m.Colour(1200, WorldLightColumns.DirectColor, true, out r, out g, out b);
            bool clampMid = r == 150;
            Check(wrap0 && clamp0 && clampEnd && clampMid && want == 136,
                  "wrap past midnight (00:00 between 15:00 and 05:00 + 24 h: 136) against the clamp arm (the first key before it, the last after; " +
                  "linear between)");

            // One key holds all day; an empty or unsorted day is refused (kept as no keys).
            var one = new WorldLightSet();
            one.SetColumns(new[] { "SkyTopColor" }, new string[0], new string[0], new string[0]);
            int k1 = one.AddDay(new[] { 900 }, new[] { 0x102030 }, new double[0], new long[0]);
            one.DayAt(k1).Colour(0, WorldLightColumns.SkyTopColor, false, out r, out g, out b);
            bool single = r == 16 && g == 32 && b == 48;
            one.DayAt(k1).Colour(2879, WorldLightColumns.SkyTopColor, false, out r, out g, out b);
            single &= r == 16 && g == 32 && b == 48;
            int bad1 = one.AddDay(new int[0], new int[0], new double[0], new long[0]);
            int bad2 = one.AddDay(new[] { 100, 100 }, new[] { 1, 2 }, new double[0], new long[0]);
            int bad3 = one.AddDay(new[] { 100, 2880 }, new[] { 1, 2 }, new double[0], new long[0]);
            int bad4 = one.AddDay(new[] { 100, 200 }, new[] { 1 }, new double[0], new long[0]);
            bool kept = one.AddParams(7, 2, 0, 0, 0, 0, null, 0, 0);
            WorldLightParams p7;
            one.TryGetParams(7, out p7);
            Check(single && bad1 == -1 && bad2 == -1 && bad3 == -1 && bad4 == -1 && one.DroppedDays == 4 && one.DayCount == 5 && !kept && p7.Day == -1 &&
                  one.DroppedParams == 1 && one.FirstDroppedDay.StartsWith("day 1: no keys", StringComparison.Ordinal),
                  "a single key holds all day; no keys, a repeated time, a time of 2880 and a short array are refused (counted, the index kept), and " +
                  "params naming a refused day have no keys (" + one.FirstDroppedDay + ")");

            // The colour's top byte (signed 25-bit storage) is masked; the absent columns are not present.
            var mask = new WorldLightSet();
            mask.SetColumns(new[] { "SunColor", "HorizonAmbientColor", "Nonsense" }, new[] { "FogEnd" }, new string[0], new[] { "HorizonAmbientColor" });
            mask.AddDay(new[] { 0 }, new[] { unchecked((int)0xFF123456), 0x010101, 0x777777 }, new[] { 1.0 }, new long[0]);
            mask.DayAt(0).Colour(0, WorldLightColumns.SunColor, false, out r, out g, out b);
            Check(r == 0x12 && g == 0x34 && b == 0x56 && mask.UnknownColumns == 1 && mask.ColourPresent(WorldLightColumns.SunColor) &&
                  !mask.ColourPresent(WorldLightColumns.HorizonAmbientColor) && !mask.ColourPresent(WorldLightColumns.DirectColor) &&
                  mask.FloatPresent(WorldLightColumns.FogEnd) && !mask.FloatPresent(WorldLightColumns.FogDensity),
                  "columns: 0xRRGGBB masked to 24 bits; an unknown column ignored and counted; a column the layout lacks (absentColumns) and one not " +
                  "sent are not present");
            string why;
            Check(WorldLightDay.From(new[] { 0 }, new[] { 0 }, new[] { double.NaN }, new long[0], new[] { 0 }, new[] { 0 }, new int[0], out why) == null &&
                  why == "a float is not finite",
                  "a float that is not finite refuses the day");
        }

        // ================================================================================== the selection

        static void SelectionTests()
        {
            WorldLightSet s = Southfury();
            var sel = new WorldLightSelection();
            var rules = new WorldLightRules();
            s.Select(1261, -3948, 20.15, 0, rules, sel);
            Check(sel.Valid && sel.Zone == 139 && sel.Light == 204 && sel.Params == 5985 && sel.Points.Count == 1 && sel.Points[0].Light == 300 &&
                  sel.Points[0].W == 1.0 && sel.Day == s.DayAt(0),
                  "Southfury (X 1261, Y -3948): zone 139 -> Light 204, LightParams 5985; the point light 300 (39 yd, within its start) at weight 1");
            s.Select(1261, -3948, 20.15, 1, rules, sel);
            Check(sel.Valid && sel.Params == 5985 && sel.SlotFellBack && sel.Slot == 1,
                  "slot 1 of Light 204 (LightParams 5987) has no keys: slot 0's (5985) are taken, and said (INFERRED)");
            s.Select(0, 0, 0, 0, rules, sel);
            Check(!sel.Valid && sel.Zone == 0 && sel.Light == 191 && sel.Why == WorldLightSelection.WhyNoKeys && sel.Points.Count == 0,
                  "outside the zone: the default Light 191, whose LightParams 192 has no keys -- invalid, no point light applied");
            var none = new WorldLightSet();
            none.Select(0, 0, 0, 0, rules, sel);
            Check(!sel.Valid && sel.Zone == -1 && sel.Light == 0 && sel.Why == WorldLightSelection.WhyNoLight,
                  "no zone and no default light: invalid");

            // Zone order and the Z band, as Stage 09: the first in the sent order wins.
            var z = new WorldLightSet { DefaultLight = 0 };
            z.SetColumns(new[] { "DirectColor" }, new string[0], new string[0], new string[0]);
            z.AddDay(new[] { 0 }, new[] { 1 }, new double[0], new long[0]);
            z.AddParams(1, 0, 0, 0, 0, 0, null, 0, 0);
            foreach (int l in new[] { 10, 20, 30 })
                z.AddLight(l, new[] { 1, 0, 0, 0, 0, 0, 0, 0 });
            z.AddZone(5, 10, 3, 0, 100, 0, Square(2, 2, 4, 4));
            z.AddZone(6, 20, 1, -10, 10, 0, Square(0, 0, 10, 10));
            z.AddZone(7, 30, 0, -64000, 64000, 1, Square(-100, -100, 100, 100));
            z.Select(3, 3, 50, 0, rules, sel);
            bool inner = sel.Zone == 5 && sel.Light == 10;
            z.Select(3, 3, 5, 0, rules, sel);
            bool band = sel.Zone == 5;
            z.Select(3, 3, 150, 0, rules, sel);
            bool above = sel.Zone == 7 && sel.Light == 30;
            z.Select(8, 8, 5, 0, rules, sel);
            bool outer = sel.Zone == 6 && sel.Light == 20;
            Check(inner && band && above && outer && z.Zones[0].Priority == 3,
                  "zones in the sent order, the Z band inclusive; the priority (TransitionType) is carried, unused by Stage 09's rule");

            // Point lights: weakest first, ties by light id (Stage 09); the arms.
            var p = new WorldLightSet { DefaultLight = 1 };
            p.SetColumns(new[] { "DirectColor" }, new[] { "FogEnd" }, new string[0], new string[0]);
            p.AddDay(new[] { 0 }, new[] { 0x000000 }, new[] { 0.0 }, new long[0]);
            p.AddDay(new[] { 0 }, new[] { 0xFFFFFF }, new[] { 100.0 }, new long[0]);
            p.AddDay(new[] { 0 }, new[] { 0x000000 }, new[] { 0.0 }, new long[0]);
            p.AddParams(100, 0, 0, 0, 0, 0, null, 0, 0);
            p.AddParams(101, 1, 0, 0, 0, 0, null, 0, 0);
            p.AddParams(102, 2, 0, 0, 0, 0, null, 0, 0);
            p.AddParams(103, -1, 0, 0, 0, 0, null, 0, 0);
            p.AddLight(1, new[] { 100, 0, 0, 0, 0, 0, 0, 0 });
            p.AddLight(10, new[] { 101, 0, 0, 0, 0, 0, 0, 0 });
            p.AddLight(11, new[] { 102, 0, 0, 0, 0, 0, 0, 0 });
            p.AddLight(12, new[] { 103, 0, 0, 0, 0, 0, 0, 0 });
            p.AddPoint(11, 0, 25, 0, 0, 100);       // d 25: w 0.75, black
            p.AddPoint(10, 75, 0, 0, 0, 100);       // d 75: w 0.25, white
            p.AddPoint(12, 0, 0, 0, 0, 100);        // no keys: never applied
            p.AddPoint(10, 0, 0, 500, 0, 100);      // 500 yd above: out of reach in 3D, w 1 in 2D
            var ev = new WorldLightEvaluator();
            var smp = new WorldLightSample();
            ev.Evaluate(p, 0, 0, 0, WorldClock.Noon, 0, rules, smp);
            double weakest = smp.V[WorldLightSample.ColourAt(WorldLightColumns.DirectColor)];
            int applied = smp.Points;
            ev.Evaluate(p, 0, 0, 0, WorldClock.Noon, 0, new WorldLightRules { PointOrder = WorldLightPointOrder.StrongestFirst }, smp);
            double strongest = smp.V[0];
            ev.Evaluate(p, 0, 0, 0, WorldClock.Noon, 0, new WorldLightRules { PointOrder = WorldLightPointOrder.FarthestFirst }, smp);
            double farthest = smp.V[0];
            ev.Evaluate(p, 0, 0, 0, WorldClock.Noon, 0, new WorldLightRules { Distance2D = true }, smp);
            double twoD = smp.V[0];
            int twoDPoints = smp.Points;
            Check(applied == 2 && Near(weakest, 0.0625) && Near(strongest, 0.25) && Near(farthest, 0.0625) && twoDPoints == 3 &&
                  Near(twoD, 1.0) && Near(smp.V[WorldLightSample.FloatAt(WorldLightColumns.FogEnd)], 100.0),
                  "point lights: weakest first (0 -> white at 0.25 -> black at 0.75: 0.0625; strongest first black then white: 0.25; farthest first " +
                  "gives weakest first's order on THIS case, so the next check tells them apart); " +
                  "one without keys never; one 500 yd above out of reach in 3D, at weight 1 with the 2D arm (applied last: white, FogEnd 100)");

            // The three orders on a case where each gives a different order and a different colour: P1 10 yd away reaching 40 (w
            // 0.75, white), P2 20 yd reaching 25 (w 0.2, black), P3 30 yd reaching 75 (w 0.6, grey 128) -- weakest first P2 P3 P1,
            // strongest first P1 P3 P2, farthest first P3 P2 P1.
            var q = new WorldLightSet { DefaultLight = 1 };
            q.SetColumns(new[] { "DirectColor" }, new string[0], new string[0], new string[0]);
            q.AddDay(new[] { 0 }, new[] { 0x000000 }, new double[0], new long[0]);
            q.AddDay(new[] { 0 }, new[] { 0xFFFFFF }, new double[0], new long[0]);
            q.AddDay(new[] { 0 }, new[] { 0x000000 }, new double[0], new long[0]);
            q.AddDay(new[] { 0 }, new[] { 0x808080 }, new double[0], new long[0]);
            for (int k = 0; k < 4; k++)
                q.AddParams(200 + k, k, 0, 0, 0, 0, null, 0, 0);
            q.AddLight(1, new[] { 200, 0, 0, 0, 0, 0, 0, 0 });
            q.AddLight(21, new[] { 201, 0, 0, 0, 0, 0, 0, 0 });
            q.AddLight(22, new[] { 202, 0, 0, 0, 0, 0, 0, 0 });
            q.AddLight(23, new[] { 203, 0, 0, 0, 0, 0, 0, 0 });
            q.AddPoint(21, 10, 0, 0, 0, 40);
            q.AddPoint(22, 0, 20, 0, 0, 25);
            q.AddPoint(23, 0, 0, 30, 0, 75);
            Func<double, double, double, double> lerp = (v, c, w) => v + (c - v) * w;
            double g = 128 / 255.0;
            double wantWeak = lerp(lerp(lerp(0, 0, 0.2), g, 0.6), 1, 0.75), wantStrong = lerp(lerp(lerp(0, 1, 0.75), g, 0.6), 0, 0.2),
                   wantFar = lerp(lerp(lerp(0, g, 0.6), 0, 0.2), 1, 0.75);
            var orders = new List<string>();
            var got = new double[3];
            WorldLightPointOrder[] arms = { WorldLightPointOrder.WeakestFirst, WorldLightPointOrder.StrongestFirst, WorldLightPointOrder.FarthestFirst };
            for (int a = 0; a < 3; a++)
            {
                ev.Evaluate(q, 0, 0, 0, WorldClock.Noon, 0, new WorldLightRules { PointOrder = arms[a] }, smp);
                got[a] = smp.V[0];
                var ids = new List<string>();
                foreach (LightGeometry.Weighted<WorldLightParams> w in ev.Selection.Points)
                    ids.Add(w.Light.ToString(CultureInfo.InvariantCulture));
                orders.Add(string.Join(" ", ids.ToArray()));
            }
            Check(orders[0] == "22 23 21" && orders[1] == "21 23 22" && orders[2] == "23 22 21" && Near(got[0], wantWeak, 1e-12) &&
                  Near(got[1], wantStrong, 1e-12) && Near(got[2], wantFar, 1e-12) && !Near(got[0], got[2], 1e-3) && !Near(got[1], got[2], 1e-3) &&
                  !Near(got[0], got[1], 1e-3),
                  string.Format(CultureInfo.InvariantCulture,
                                "point-light order arms on a discriminating case: weakest first {0} -> {1:0.####}, strongest first {2} -> {3:0.####}, " +
                                "farthest first {4} -> {5:0.####}: three orders, three colours", orders[0], got[0], orders[1], got[1], orders[2], got[2]));

            // Copy rows (review finding 3): a LightParams with no keys of its own names its copy source's day; the selection reports
            // the copy row's id, and its values are the source's.
            var cp = new WorldLightSet { DefaultLight = 0 };
            cp.SetColumns(new[] { "DirectColor", "SkyTopColor" }, new[] { "FogEnd" }, new[] { "ColorGradingFileDataID" }, new string[0]);
            int sourceDay = cp.AddDay(new[] { 0, 1440 }, new[] { 0x102030, 0x405060, 0xA0B0C0, 0xD0E0F0 }, new[] { 100.0, 300.0 }, new long[] { 7, 8 });
            cp.AddParams(6000, sourceDay, 0.25, 3, 0, 0, null, 0, 0);       // the source
            cp.AddParams(6001, sourceDay, 0.75, 4, 0, 0, null, 0, 0);       // the copy row: the source's day
            cp.AddLight(31, new[] { 6000, 0, 0, 0, 0, 0, 0, 0 });
            cp.AddLight(32, new[] { 6001, 0, 0, 0, 0, 0, 0, 0 });
            cp.AddZone(1, 31, 0, -100, 100, 0, Square(0, 0, 10, 10));
            cp.AddZone(2, 32, 0, -100, 100, 0, Square(20, 0, 30, 10));
            var src = new WorldLightSample();
            ev.Evaluate(cp, 5, 5, 0, 9 * 3600.0, 0, rules, src);
            var copy = new WorldLightSample();
            ev.Evaluate(cp, 25, 5, 0, 9 * 3600.0, 0, rules, copy);
            cp.Select(25, 5, 0, 0, rules, sel);
            bool sameValues = copy.Valid && src.Valid && copy.Params == 6001 && src.Params == 6000 && sel.Params == 6001 && sel.Day == cp.DayAt(sourceDay);
            for (int k = 0; sameValues && k < WorldLightSample.Glow; k++)
                sameValues = copy.V[k] == src.V[k];
            sameValues &= copy.Ints[0] == src.Ints[0] && copy.V[WorldLightSample.Glow] == 0.75 && copy.Skybox == 4 && src.Skybox == 3;
            // The same object through the runner's own JSON reader (FromJson: the wire object's shape; not the player's parser,
            // WmvIpcClient.ParseWorldLight, which the lifecycle self-test reads lines with).
            string json = "{\"state\":\"ok\",\"map\":0,\"defaultLight\":0,\"zones\":[{\"zone\":2,\"light\":32,\"zmin\":-100,\"zmax\":100," +
                          "\"conditional\":0,\"poly\":[20,0,30,0,30,10,20,10]}],\"points\":[],\"lights\":[{\"light\":32,\"slots\":[6001,0,0,0,0,0,0,0]}]," +
                          "\"params\":[{\"id\":6000,\"day\":0,\"glow\":0,\"skybox\":0,\"flags\":0,\"highlightSky\":0,\"sunPolar\":0,\"sunAzimuth\":0}," +
                          "{\"id\":6001,\"day\":0,\"glow\":0,\"skybox\":0,\"flags\":0,\"highlightSky\":0,\"sunPolar\":0,\"sunAzimuth\":0}]," +
                          "\"colorColumns\":[\"DirectColor\"],\"floatColumns\":[],\"intColumns\":[],\"days\":[{\"times\":[0],\"c\":[1193046],\"f\":[],\"i\":[]}]}";
            WorldLightSet fromJson;
            using (JsonDocument doc = JsonDocument.Parse(json))
                fromJson = FromJson(doc.RootElement);
            fromJson.Select(25, 5, 0, 0, rules, sel);
            bool jsonCopy = sel.Valid && sel.Params == 6001 && sel.Day == fromJson.DayAt(0) && fromJson.DroppedParams == 0 && fromJson.ParamsCount == 2;
            Check(sameValues && jsonCopy,
                  "copy rows: LightParams 6001 naming its source 6000's day is selected as 6001 (runtimeState's id) with every colour, float and " +
                  "integer the source's at 09:00 (its own Glow and skybox); the same through the runner's own JSON reader (not the player's " +
                  "parser)");
        }

        // ================================================================================== the sample, clock, ease, switches, model

        static void SampleTests()
        {
            WorldLightSet s = Southfury();
            var ev = new WorldLightEvaluator();
            var smp = new WorldLightSample();
            ev.Evaluate(s, 1261, -3948 - 1000, 20, WorldClock.Noon, 0, new WorldLightRules(), smp);   // outside the zone: invalid default
            bool invalid = !smp.Valid && smp.Light == 191 && smp.Groups == 0 && smp.V[0] == 0;
            // Inside the zone, half-way along the point light's ramp (75 yd: w 0.5): every colour half-way to 255, Glow half-way to 1.
            ev.Evaluate(s, 1300 - 75, -3948, 20, WorldClock.Noon, 0, new WorldLightRules(), smp);
            double r, g, b;
            smp.Colour(WorldLightColumns.SkyMiddleColor, out r, out g, out b);
            bool mixed = smp.Valid && smp.Points == 1 && Near(r, 1.0) && Near(g, (226 / 255.0 + 1.0) / 2) && Near(b, (175 / 255.0 + 1.0) / 2) &&
                         Near(smp.Float(WorldLightColumns.FogDensity), 6.0) && Near(smp.V[WorldLightSample.Glow], 0.5) && smp.Ints[0] == 0 &&
                         smp.BaseRgb[3 * WorldLightColumns.SkyMiddleColor + 1] == 226 && smp.HalfMinute == 1440 && smp.Params == 5985;
            int want = WorldLightSample.GroupGlow | WorldLightSample.GroupSky | WorldLightSample.GroupAmbient | WorldLightSample.GroupDirect |
                       WorldLightSample.GroupWater | WorldLightSample.GroupFog | WorldLightSample.GroupGrading;
            Check(invalid && mixed && smp.Groups == want,
                  "sample: invalid where the base light has no keys (no group valid); inside zone 139 with the point light at weight 0.5 every colour and " +
                  "float half-way (SkyMiddle G (226/255 + 1) / 2, FogDensity 6), Glow 0.5, the integers and BaseRgb the base light's; groups " +
                  WorldLightSample.DescribeGroups(smp.Groups) + " (the columns this object sends)");
            Check(WorldLightSample.Count == 3 * 24 + 26 + 1 + 17 && WorldLightSample.FogBase == 3 * 24 + 26 + 1 && WorldFog.DerivedCount == 17 &&
                  WorldLightSample.GroupOfColour(WorldLightColumns.EndFogColor) == WorldLightSample.GroupFogColours &&
                  WorldLightSample.GroupOfFloat(WorldLightColumns.CloudDensity) == WorldLightSample.GroupClouds &&
                  WorldLightSample.GroupOfColour(WorldLightColumns.ShadowOpacity) == WorldLightSample.GroupShadow,
                  "sample layout: 24 colours x 3, 26 floats, Glow, then (step 7) the 17 derived fog channels (116 channels); EndFogColor a fog colour, " +
                  "CloudDensity a cloud channel, ShadowOpacity its own");
        }

        static void ClockTests()
        {
            double s;
            bool p1 = WorldClock.TryParse("12:00:00", out s) && WorldClock.HalfMinuteOf(s) == 1440;
            bool p2 = WorldClock.TryParse("12:00:29", out s) && WorldClock.HalfMinuteOf(s) == 1440;
            bool p3 = WorldClock.TryParse("12:00:30", out s) && WorldClock.HalfMinuteOf(s) == 1441;
            bool p4 = WorldClock.TryParse("23:59:59", out s) && WorldClock.HalfMinuteOf(s) == 2879;
            bool p5 = WorldClock.TryParse("00:00", out s) && WorldClock.HalfMinuteOf(s) == 0 && s == 0;
            bool p6 = WorldClock.TryParse("6:05", out s) && WorldClock.HalfMinuteOf(s) == 2 * (60 * 6 + 5);
            bool bad = !WorldClock.TryParse("24:00", out s) && !WorldClock.TryParse("12:60", out s) && !WorldClock.TryParse("12", out s) &&
                       !WorldClock.TryParse("12:00:00:00", out s) && !WorldClock.TryParse("ab:cd", out s) && !WorldClock.TryParse("", out s) &&
                       !WorldClock.TryParse(null, out s) && !WorldClock.TryParse("-1:00", out s) && !WorldClock.TryParse("123:00", out s);
            Check(p1 && p2 && p3 && p4 && p5 && p6 && bad && WorldClock.Format(43200) == "12:00:00" && WorldClock.Format(86399.9) == "23:59:59" &&
                  WorldClock.Format(-30) == "23:59:30",
                  "the half-minute from HH:MM[:SS]: floor(seconds / 30) -- 12:00:00 and 12:00:29 1440, 12:00:30 1441, 23:59:59 2879, 6:05 730; " +
                  "24:00, 12:60, a lone hour, four fields, letters, nothing, a sign and three digits refused; formatted back as HH:MM:SS");
            var c = new WorldClock();
            bool pinned = c.Source == WorldClock.SourcePinned && c.HalfMinute == 1440 && c.Pinned;
            c.Advance(100);
            bool still = c.Seconds == WorldClock.Noon;
            c.Running = true;
            c.Seconds = 86400 - 10;
            c.Advance(25);
            bool wrapped = Near(c.Seconds, 15) && c.Source == WorldClock.SourceRunning && !c.Pinned;
            c.Advance(double.NaN);
            c.Advance(-5);
            bool ignored = Near(c.Seconds, 15);
            c.MapOverrideMinutes = 360;
            bool map = c.Source == WorldClock.SourceMap && c.HalfMinute == 720 && c.Effective == 21600 && c.Pinned;
            c.Advance(60);
            bool runsUnder = Near(c.Seconds, 75) && c.HalfMinute == 720;
            c.MapOverrideMinutes = 1440;
            bool outside = c.Source == WorldClock.SourceRunning;
            Check(pinned && still && wrapped && ignored && map && runsUnder && outside,
                  "clock: pinned at 12:00:00 by default (does not move); running 1 s a second and wrapping at midnight (23:59:50 + 25 s = 00:00:15); " +
                  "no time or a negative one ignored; a map override (360 min) wins (06:00, \"map\", pinned) while the clock runs on under it; an " +
                  "override outside the day is not one");

            // The time override from the map's object: it reaches the clock and the sample, and its coming and going is a clock jump.
            WorldLightSet sf = Southfury();
            sf.TimeOverride = 360;
            var clock = new WorldClock();
            bool came = clock.UseMap(sf);
            bool same = !clock.UseMap(sf);
            var ev = new WorldLightEvaluator();
            var smp = new WorldLightSample();
            ev.Evaluate(sf, 1261, -3948, 20, clock.Effective, 0, new WorldLightRules(), smp);
            bool morning = smp.HalfMinute == 720 && Rgb(smp.BaseRgb, WorldLightColumns.DirectColor, 120, 84, 0) && clock.Source == WorldClock.SourceMap;
            sf.TimeOverride = 65535;
            bool went = clock.UseMap(sf) && clock.Source == WorldClock.SourcePinned && clock.HalfMinute == 1440 && sf.TimeOverrideMinutes == -1;
            bool none = !clock.UseMap(null) && clock.MapOverrideMinutes == -1;
            Check(came && same && morning && went && none && sf.Summary().Contains("time override none (sent 65535)"),
                  "time override: Map.TimeOfDayOverride 360 min takes the light to 06:00 (Southfury Direct (120,84,0)) and is a clock jump when it comes " +
                  "and goes; 65535 (a 16-bit -1) and no object are none -- the pinned 12:00:00 again");
        }

        static WorldLightSample Flat(bool valid, double v, int light = 1)
        {
            var s = new WorldLightSample { Valid = valid, Light = valid ? light : 0, Zone = 1 };
            for (int k = 0; k < WorldLightSample.Count; k++) s.V[k] = v;
            return s;
        }

        static void EaseTests()
        {
            object a = new object(), b = new object();
            var e = new WorldLightEase();
            string r1 = e.Step(Flat(true, 0.2), a, 0, 0, 0, 0.016, false, false);
            string r2 = e.Step(Flat(true, 0.6), a, 10, 0, 0, 0.25, false, false);
            bool eased = r1 == LiquidLightEase.SnapFirst && r2 == null && Near(e.V[0], 0.2 + 0.4 * (1 - Math.Exp(-1))) &&
                         Near(e.V[WorldLightSample.Count - 1], e.V[0]);
            string r3 = e.Step(Flat(true, 0.9), a, 10, 0, 0, 0.016, false, true);
            bool clock = r3 == WorldLightEase.SnapClock && e.V[0] == 0.9;
            string r4 = e.Step(Flat(true, 0.1), a, 10, 0, 0, 0.016, true, false);
            bool pinned = r4 == LiquidLightEase.SnapPinned && e.V[50] == 0.1;
            string r5 = e.Step(Flat(true, 0.3), a, 80, 0, 0, 0.016, false, false);
            bool jump = r5 == LiquidLightEase.SnapJump && e.V[0] == 0.3;
            string r6 = e.Step(Flat(false, 0.7), a, 80, 0, 0, 0.016, false, false);
            bool invalid = r6 == LiquidLightEase.Invalid && !e.Valid && e.V[0] == 0.3;
            string r7 = e.Step(Flat(true, 0.5), b, 80, 0, 0, 0.016, false, false);
            bool other = r7 == LiquidLightEase.SnapLights && e.V[0] == 0.5;
            e.Reset();
            string r8 = e.Step(Flat(true, 0.4), b, 80, 0, 0, 0.016, false, false);
            Check(eased && clock && pinned && jump && invalid && other && r8 == LiquidLightEase.SnapFirst,
                  "ease (Stage 09's constants over every channel): first snaps, then 1 - exp(-dt / 0.25 s); a clock jump, pinned clocks (the driver " +
                  "passes -wmvAnimTime's, as the water's), a move over 60 " +
                  "yd, another object snap; an invalid sample keeps the channels (" + r1 + ", " + (r2 ?? "eased") + ", " + r3 + ", " + r4 + ", " + r5 + ", " +
                  r6 + ", " + r7 + ", " + r8 + ")");
        }

        static void SwitchTests()
        {
            WorldLightSwitches d = WorldLightSwitches.Parse(new string[0], null);
            bool defaults = !d.HardOff && !d.On && d.TimeSeconds == WorldClock.Noon && !d.TimeGiven && d.FarClip == 7000 && d.Rules.IsDefault &&
                            d.Notes.Count == 0;
            WorldLightSwitches off = WorldLightSwitches.Parse(new[] { "-wmvWorldLight=on" }, "off");
            WorldLightSwitches envOn = WorldLightSwitches.Parse(new string[0], "ON");
            WorldLightSwitches cmdOn = WorldLightSwitches.Parse(new[] { "-wmvWorldLight=on", "-wmvGameTime=06:30", "-wmvGameFarclip=1500" }, null);
            WorldLightSwitches cmdOff = WorldLightSwitches.Parse(new[] { "-wmvWorldLight=off" }, "on");
            WorldLightSwitches bad = WorldLightSwitches.Parse(new[] { "-wmvWorldLight=yes", "-wmvGameTime=25:00", "-wmvGameFarclip=-3", "-wmvGameFarclip=x",
                                                                      "-wmvLightRule=nearest", "-wmvLightWrap=loop" }, "maybe");
            WorldLightSwitches arms = WorldLightSwitches.Parse(new[] { "-wmvLightRule=farthest+2d", "-wmvLightWrap=clamp" }, null);
            Check(defaults && off.HardOff && !off.On && envOn.On && cmdOn.On && cmdOn.TimeGiven && cmdOn.TimeSeconds == 6.5 * 3600 && cmdOn.FarClip == 1500 &&
                  !cmdOff.On && bad.Notes.Count == 7 && !bad.On && bad.FarClip == 7000 && bad.TimeSeconds == WorldClock.Noon && bad.Rules.IsDefault &&
                  arms.Rules.PointOrder == WorldLightPointOrder.FarthestFirst && arms.Rules.Distance2D && arms.Rules.Clamp,
                  "switches: off by default (12:00:00, far clip 7000, Stage 09's rule); WMV_LIGHT=off hard off whatever else; WMV_LIGHT=on or " +
                  "-wmvWorldLight=on opt in, -wmvWorldLight=off wins over the environment; -wmvGameTime, -wmvGameFarclip; every bad value a note " +
                  "(" + bad.Notes.Count + "); the debug arms (" + arms.Rules + ")");

            // Steps 4 and 5: the sky's and the terrain's switches.
            WorldLightSwitches draw = WorldLightSwitches.Parse(new[] { "-wmvSky=off", "-wmvSkyTable=b", "-wmvSkyEndFog=off", "-wmvTerrainLight=off",
                                                                       "-wmvTerrainVariant=flat" }, "on");
            WorldLightSwitches drawBad = WorldLightSwitches.Parse(new[] { "-wmvSky=no", "-wmvSkyTable=c", "-wmvSkyEndFog=1", "-wmvTerrainLight=x",
                                                                          "-wmvTerrainVariant=round" }, null);
            WorldLightSwitches drawBack = WorldLightSwitches.Parse(new[] { "-wmvSkyTable=b", "-wmvSkyTable=a", "-wmvTerrainVariant=flat",
                                                                           "-wmvTerrainVariant=hemisphere", "-wmvSky=off", "-wmvSky=on" }, null);
            Check(d.DrawDefaults && d.Sky && !d.SkyTableB && d.SkyEndFog && d.TerrainLight && !d.TerrainFlat &&
                  !draw.Sky && draw.SkyTableB && !draw.SkyEndFog && !draw.TerrainLight && draw.TerrainFlat && draw.On && draw.Notes.Count == 0 &&
                  drawBad.DrawDefaults && drawBad.Notes.Count == 5 && drawBack.DrawDefaults && drawBack.Notes.Count == 0 &&
                  draw.Describe().Contains("sky off (-wmvSky=off)") && d.Describe().Contains("sky: the colour dome, table a"),
                  "switches (steps 4 and 5): by default the sky (table a, ring 5 leaning to EndFogColor) and the lit terrain (hemisphere); " +
                  "-wmvSky=off, -wmvSkyTable=b, -wmvSkyEndFog=off, -wmvTerrainLight=off, -wmvTerrainVariant=flat each taken; a bad value a note " +
                  "(" + drawBad.Notes.Count + ") and the default kept; the last of two values wins (" + draw.DescribeDraw() + ")");

            // Step 6: the doodads' and world models' switches (they act where the world renderers are bound).
            WorldLightSwitches models = WorldLightSwitches.Parse(new[] { "-wmvDoodadLight=off", "-wmvWorldModelLight=off", "-wmvDoodadVariant=flat",
                                                                         "-wmvWorldModelVariant=flat", "-wmvInteriorLight=outdoor", "-wmvWorldLitView=albedo" }, "on");
            WorldLightSwitches modelsBad = WorldLightSwitches.Parse(new[] { "-wmvDoodadLight=no", "-wmvWorldModelLight=1", "-wmvDoodadVariant=round",
                                                                            "-wmvWorldModelVariant=", "-wmvInteriorLight=indoor", "-wmvWorldLitView=none" }, null);
            WorldLightSwitches modelsBack = WorldLightSwitches.Parse(new[] { "-wmvDoodadLight=off", "-wmvDoodadLight=on", "-wmvDoodadVariant=flat",
                                                                             "-wmvDoodadVariant=hemisphere", "-wmvWorldModelVariant=flat",
                                                                             "-wmvWorldModelVariant=hemisphere", "-wmvInteriorLight=outdoor",
                                                                             "-wmvInteriorLight=rig", "-wmvWorldLitView=albedo", "-wmvWorldLitView=normal" }, null);
            WorldLightSwitches lit = WorldLightSwitches.Parse(new[] { "-wmvDoodadVariant=flat" }, null);
            string dd = d.DescribeDraw(), md = models.DescribeDraw(), nd = modelsBack.DescribeDraw(), ld = lit.DescribeDraw();
            Check(d.DoodadLight && d.WorldModelLight && !d.DoodadFlat && !d.WorldModelFlat && !d.InteriorOutdoor &&
                  d.WorldLitView == WorldLightSwitches.WorldLitViewNone &&
                  !models.DoodadLight && !models.WorldModelLight && models.DoodadFlat && models.WorldModelFlat && models.InteriorOutdoor &&
                  models.WorldLitView == WorldLightSwitches.WorldLitViewAlbedo && models.On && models.Notes.Count == 0 && !models.DrawDefaults &&
                  modelsBad.DrawDefaults && modelsBad.Notes.Count == 6 && modelsBad.Notes.TrueForAll(n => n.Contains("ignored") || n.Contains(" -- ")) &&
                  modelsBack.DoodadLight && !modelsBack.DoodadFlat && !modelsBack.WorldModelFlat && !modelsBack.InteriorOutdoor &&
                  modelsBack.WorldLitView == WorldLightSwitches.WorldLitViewNormal && modelsBack.Notes.Count == 0 && !modelsBack.DrawDefaults &&
                  !lit.DrawDefaults && d.DrawDefaults,
                  "switches (step 6): by default doodads and world models lit (hemisphere), interiors on the rig, no diagnostic view; " +
                  "-wmvDoodadLight=off, -wmvWorldModelLight=off, -wmvDoodadVariant=flat, -wmvWorldModelVariant=flat, -wmvInteriorLight=outdoor, " +
                  "-wmvWorldLitView=albedo|normal each taken; a bad value a note (" + modelsBad.Notes.Count + ") and the default kept; the last of " +
                  "two values wins; DrawDefaults false with any of them");
            Check(dd.Contains("; doodads: outdoor lit (hemisphere variant), interior as Stage 09; world models: exterior groups lit (hemisphere " +
                              "variant), interior groups on the preview rig until Stage 12; exterior MOCV not drawn; water as Stage 09; fog: the " +
                              "client's per-pixel fog") &&
                  !dd.Contains("DIAGNOSTIC") && md.Contains("doodads as Stage 09 (-wmvDoodadLight=off)") &&
                  md.Contains("world models as Stage 09 (-wmvWorldModelLight=off)") && md.Contains("DIAGNOSTIC VIEW (-wmvWorldLitView=albedo)") &&
                  nd.Contains("DIAGNOSTIC VIEW (-wmvWorldLitView=normal)") && ld.Contains("FLAT variant (-wmvDoodadVariant=flat, for the record)") &&
                  WorldLightSwitches.Parse(new[] { "-wmvInteriorLight=outdoor" }, null).DescribeDraw().Contains("interior groups lit too (-wmvInteriorLight=outdoor") &&
                  d.Describe().Contains("doodads: outdoor lit"),
                  "DescribeDraw (step 6): \"" + dd + "\"; the switches' departures named (" + md + ")");

            // Step 7: the fog's switches (Tests/WorldFogTests.cs R7 checks their texts).
            WorldLightSwitches fog = WorldLightSwitches.Parse(new[] { "-wmvFog=off", "-wmvFogClassic=public" }, "on");
            WorldLightSwitches fogBad = WorldLightSwitches.Parse(new[] { "-wmvFog=no", "-wmvFogClassic=yes" }, null);
            Check(d.Fog && d.FogClassic == WorldFogClassicArm.Off && !fog.Fog && fog.FogClassic == WorldFogClassicArm.Public && !fog.DrawDefaults &&
                  fog.Notes.Count == 0 && fogBad.Fog && fogBad.FogClassic == WorldFogClassicArm.Off && fogBad.Notes.Count == 2 && fogBad.DrawDefaults &&
                  fog.Describe().Contains("fog OFF (-wmvFog=off)"),
                  "switches (step 7): the fog on and no -wmvFogClassic arm by default; -wmvFog=off and -wmvFogClassic=public taken; a bad value a note (" +
                  fogBad.Notes.Count + ") and the default kept");
        }

        static void ModelTests()
        {
            double x, y, z;
            WorldLightModel.LightDirection(0, out x, out y, out z);
            bool at0 = Near(Math.Asin(z) * 180 / Math.PI, 37, 1e-9) && Near(x, y) && x > 0 && Near(x * x + y * y + z * z, 1.0, 1e-12);
            WorldLightModel.LightDirection(6 * 3600, out x, out y, out z);
            bool at6 = Near(Math.Asin(z) * 180 / Math.PI, 20, 1e-9);
            WorldLightModel.LightDirection(9 * 3600, out x, out y, out z);
            bool at9 = Near(Math.Asin(z) * 180 / Math.PI, 28.5, 1e-9);
            WorldLightModel.LightDirection(21 * 3600, out x, out y, out z);
            bool at21 = Near(Math.Asin(z) * 180 / Math.PI, 28.5, 1e-9);
            bool at12 = Near(WorldLightModel.LightElevationDeg(12 * 3600), 37) && Near(WorldLightModel.LightElevationDeg(18 * 3600), 20) &&
                        Near(WorldLightModel.LightElevationDeg(24 * 3600), 37);
            Check(at0 && at6 && at9 && at21 && at12 && WorldLightModel.LightDirectionGrade == "MEASURED" && WorldLightModel.AzimuthGrade == "MEASURED",
                  "light direction: from the north-west (game +X +Y), 37 deg at 00:00 and 12:00, 20 deg at 06:00 and 18:00, linear between " +
                  "(28.5 at 09:00 and 21:00), unit length; the public table graded MEASURED (within 2.5 deg at three times in step 1), its " +
                  "azimuth too");
            bool sun = Near(WorldLightModel.SunPolarDeg(6 * 3600), 100) && Near(WorldLightModel.SunPolarDeg(11 * 3600 + 55 * 60), 5) &&
                       Near(WorldLightModel.SunPolarDeg(12 * 3600), 5) && Near(WorldLightModel.SunPolarDeg(12 * 3600 + 5 * 60), 5) &&
                       Near(WorldLightModel.SunPolarDeg(19 * 3600), 100) && Near(WorldLightModel.SunPolarDeg(3 * 3600), 100) &&
                       Near(WorldLightModel.SunPolarDeg(22 * 3600), 100) &&
                       Near(WorldLightModel.SunPolarDeg(9 * 3600), 100 + (5 - 100) * (3.0 / (5 + 55.0 / 60)), 1e-9);
            WorldLightModel.SunDirection(12 * 3600, out x, out y, out z);
            bool noon = Near(z, Math.Cos(5 * Math.PI / 180), 1e-12) && Near(x, y) && x > 0;
            WorldLightModel.SunDirection(3 * 3600, out x, out y, out z);
            bool night = z < 0;
            Check(sun && noon && night && WorldLightModel.SunPathGrade == "INFERRED (public)" && WorldLightModel.SunPathOutsideGrade == "UNKNOWN",
                  "sun path: polar 100 deg at 06:00, 5 from 11:55 to 12:05, 100 at 19:00, linear between; held at 100 (below the horizon) outside, " +
                  "graded UNKNOWN; towards the north-west");
            bool graded = true;
            foreach (GradedValue v in WorldLightModel.Table())
                graded &= v.Grade == "PROVEN_BYTES" || v.Grade == "PROVEN_SOURCE" || v.Grade == "MEASURED" || v.Grade == "INFERRED" ||
                          v.Grade == "INFERRED (public)" || v.Grade == "UNKNOWN" || v.Grade == "ASSUMED";   // ASSUMED: step 7 after T7
            Check(graded && WorldLightModel.Table().Count == 5 + 4 + 1 + WorldLightModel.Fog.Length + WorldLightModel.Rings.Length + WorldLightModel.Sky.Length +
                  WorldLightModel.Terrain.Length + WorldLightModel.Models.Length && WorldLightModel.Rings.Length == 5 && WorldLightModel.DefaultFarClip == 7000,
                  "every constant of the model's table (light direction, sun path, far clip, fog, the dome's rings, the sky, the terrain light, the " +
                  "model light) carries one of the plan's grades");
            // Step 6: the model light's rows carry the values ModelLight.cs uses.
            var byName = new Dictionary<string, GradedValue>();
            foreach (GradedValue v in WorldLightModel.Models)
                byName[v.Name] = v;
            Check(WorldLightModel.Models.Length == byName.Count && byName["model wrap base"].Value == 0.7 && byName["model wrap scale"].Value == 0.4 &&
                  byName["model wrap half (0.5 + 0.5 N.L, N.L not saturated)"].Value == 0.5 &&
                  byName["world-model base factor, green (2 x 0.49999)"].Value == 2.0 * 0.49999 &&
                  byName["world-model base factor, red and blue (2 x 0.5)"].Value == 1.0 &&
                  byName["M2 base factor (combiner x _Color, nothing added)"].Value == 1.0 &&
                  byName["doodad variant (0 hemisphere, 1 flat)"].Grade == "MEASURED" && byName["world-model variant (0 hemisphere, 1 flat)"].Grade == "INFERRED" &&
                  byName["world-model MOCV light E (deferred)"].Value == 0.0 && byName["world-model MOCV light E (deferred)"].Grade == "UNKNOWN" &&
                  byName["shadow (held; on D only)"].Value == 1.0 && byName["combine sqrt((base F)^2 + base^2 local): local light (held)"].Value == 0.0,
                  "the model light's table (" + WorldLightModel.Models.Length + " rows, names unique): the wrap 0.7 / 0.4 / 0.5, the bases (M2 1, " +
                  "world model 1 / 0.99998), the variants (doodads hemisphere MEASURED, world models INFERRED), the holds (shadow 1 on D only, " +
                  "local 0, the MOCV light 0 UNKNOWN)");
        }

        static void GeometryTests()
        {
            var l = new List<LightGeometry.Weighted<int>>();
            LightGeometry.InsertInOrder(l, 0.5, 0.5, 21, 1);
            LightGeometry.InsertInOrder(l, 0.75, 0.75, 30, 2);
            LightGeometry.InsertInOrder(l, 0.5, 0.5, 20, 3);
            LightGeometry.InsertInOrder(l, 0.25, 0.9, 40, 4);
            Check(l.Count == 4 && l[0].Light == 40 && l[0].W == 0.9 && l[1].Light == 20 && l[2].Light == 21 && l[3].Light == 30,
                  "shared order: ascending key, ties by light id, the weight carried apart from the key");
            double[] lshape = { 40, 0, 50, 0, 50, 4, 44, 4, 44, 10, 40, 10 };
            double a, b, c, d;
            LightGeometry.Bounds(lshape, out a, out b, out c, out d);
            Check(LightGeometry.Contains(lshape, a, b, c, d, 42, 7) && !LightGeometry.Contains(lshape, a, b, c, d, 47, 7) && a == 40 && b == 50 && c == 0 && d == 10 &&
                  LightGeometry.PointWeight(10, 30, 20) == 0.5 && LightGeometry.PointWeight(40, 30, 0) == 1 && LightGeometry.PointWeight(10, 30, 30) == 0 &&
                  !LightGeometry.ValidPolygon(new double[] { 0, 0, 1, 1 }) && !LightGeometry.ValidPolygon(new double[] { 0, 0, 1, 0, double.NaN, 1 }),
                  "shared geometry: a concave polygon's notch, bounds, the point weight ramp, polygon validity (LiquidLightTests checks the same through " +
                  "the liquids, unchanged)");
        }

        // ================================================================================== real data (fixtures)

        static int[] Ints(JsonElement e)
        {
            var a = new int[e.GetArrayLength()];
            int k = 0;
            foreach (JsonElement v in e.EnumerateArray()) a[k++] = (int)v.GetInt64();
            return a;
        }

        static long[] Longs(JsonElement e)
        {
            var a = new long[e.GetArrayLength()];
            int k = 0;
            foreach (JsonElement v in e.EnumerateArray()) a[k++] = v.GetInt64();
            return a;
        }

        static double[] Doubles(JsonElement e)
        {
            var a = new double[e.GetArrayLength()];
            int k = 0;
            foreach (JsonElement v in e.EnumerateArray()) a[k++] = v.GetDouble();
            return a;
        }

        /// <summary>A float32 column as the player reads it (WmvIpcClient's float fields): the decimal to the nearest float, widened.</summary>
        static double[] Singles(JsonElement e)
        {
            var a = new double[e.GetArrayLength()];
            int k = 0;
            foreach (JsonElement v in e.EnumerateArray()) a[k++] = v.GetSingle();
            return a;
        }

        static string[] Strings(JsonElement e)
        {
            var a = new string[e.GetArrayLength()];
            int k = 0;
            foreach (JsonElement v in e.EnumerateArray()) a[k++] = v.GetString();
            return a;
        }

        static JsonElement Get(JsonElement o, string name)
        {
            JsonElement v;
            return o.TryGetProperty(name, out v) ? v : default(JsonElement);
        }

        static bool Has(JsonElement o, string name) { JsonElement v; return o.ValueKind == JsonValueKind.Object && o.TryGetProperty(name, out v); }

        /// <summary>A "worldLight" object into the set, in the order WmvIpcClient.WorldLightFrom fills it (columns, days, params,
        /// lights, zones, points).</summary>
        public static WorldLightSet FromJson(JsonElement o)
        {
            var s = new WorldLightSet
            {
                State = Has(o, "state") ? Get(o, "state").GetString() : "", Detail = Has(o, "detail") ? Get(o, "detail").GetString() ?? "" : "",
                Product = Has(o, "product") ? Get(o, "product").GetString() : "", Build = Has(o, "build") ? Get(o, "build").GetString() : "",
                Map = Has(o, "map") ? Get(o, "map").GetInt32() : 0, Branch = Has(o, "branch") ? Get(o, "branch").GetString() : "",
                LightDataLayout = Has(o, "lightDataLayout") ? Get(o, "lightDataLayout").GetString() : "",
                TimeOverride = Has(o, "timeOverride") ? Get(o, "timeOverride").GetInt32() : -1,
                DefaultLight = Has(o, "defaultLight") ? Get(o, "defaultLight").GetInt32() : 0,
                DefaultFrom = Has(o, "defaultFrom") ? Get(o, "defaultFrom").GetString() : "",
            };
            if (Has(o, "mapFlags"))
            {
                long[] f = Longs(Get(o, "mapFlags"));
                for (int k = 0; k < 3 && k < f.Length; k++) s.MapFlags[k] = f[k];
            }
            if (s.State != "ok")
                return s;
            s.SetColumns(Strings(Get(o, "colorColumns")), Strings(Get(o, "floatColumns")), Strings(Get(o, "intColumns")),
                         Has(o, "absentColumns") ? Strings(Get(o, "absentColumns")) : new string[0]);
            foreach (JsonElement d in Get(o, "days").EnumerateArray())
                s.AddDay(Ints(Get(d, "times")), Ints(Get(d, "c")), Singles(Get(d, "f")), Longs(Get(d, "i")));
            foreach (JsonElement p in Get(o, "params").EnumerateArray())
                s.AddParams(Get(p, "id").GetInt32(), Get(p, "day").GetInt32(), Get(p, "glow").GetSingle(), Get(p, "skybox").GetInt32(), Get(p, "flags").GetInt64(),
                            Get(p, "highlightSky").GetInt32(), Has(p, "overrideSun") ? Singles(Get(p, "overrideSun")) : null, Get(p, "sunPolar").GetSingle(),
                            Get(p, "sunAzimuth").GetSingle());
            foreach (JsonElement l in Get(o, "lights").EnumerateArray())
                s.AddLight(Get(l, "light").GetInt32(), Ints(Get(l, "slots")));
            foreach (JsonElement z in Get(o, "zones").EnumerateArray())
                s.AddZone(Get(z, "zone").GetInt32(), Get(z, "light").GetInt32(), Has(z, "priority") ? Get(z, "priority").GetInt32() : 0, Get(z, "zmin").GetDouble(),
                          Get(z, "zmax").GetDouble(), Get(z, "conditional").GetInt32(), Doubles(Get(z, "poly")));
            foreach (JsonElement p in Get(o, "points").EnumerateArray())
                s.AddPoint(Get(p, "light").GetInt32(), Get(p, "x").GetDouble(), Get(p, "y").GetDouble(), Get(p, "z").GetDouble(), Get(p, "start").GetDouble(),
                           Get(p, "end").GetDouble());
            return s;
        }

        static readonly string[] Clients = { "wow", "wowt", "wow_classic_era", "wow_classic", "wow_classic_beta" };
        static readonly string[] SampledNames = { "riverClose", "riverFar", "oceanClose", "oceanFar", "sky" };
        static readonly int[] SampledColumns = { WorldLightColumns.RiverCloseColor, WorldLightColumns.RiverFarColor, WorldLightColumns.OceanCloseColor,
                                                 WorldLightColumns.OceanFarColor, WorldLightColumns.SkyBand1Color };

        /// <summary>SHA-256 of the offline fixtures' manifest.json (D:\wmv-10\step3\player\fixtures, written 2026-10-21 by
        /// gen_fixtures.py), which pins the generator, its inputs and every fixture file written: a regenerated fixture is a
        /// failure here until this pin is updated on purpose.</summary>
        internal const string FixtureManifestSha256 = "5d1339442a4f7886d082473c91f0a2ab0b085e494c40b1c01103caf1967c994b";

        static void FixtureTests(string folder)
        {
            // The manifest first (pinned), then every fixture file read against the manifest's own pin of it.
            string manifestPath = Path.Combine(folder, "manifest.json");
            string manifestSha = File.Exists(manifestPath) ? TerrainCorpusTests.Sha256(manifestPath) : "missing";
            Check(manifestSha == FixtureManifestSha256, "fixtures: manifest.json has its pinned SHA-256 (" + manifestSha.Substring(0, Math.Min(16, manifestSha.Length)) + ")");
            if (manifestSha != FixtureManifestSha256)
                return;
            var written = new Dictionary<string, string>();
            using (JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(manifestPath)))
                foreach (JsonProperty f in Get(doc.RootElement, "written").EnumerateObject())
                    written[f.Name] = f.Value.GetString();
            int pinnedFiles = 0, changedFiles = 0;
            string firstChanged = null;
            foreach (KeyValuePair<string, string> f in written)
            {
                string path = Path.Combine(folder, f.Key);
                string sha = File.Exists(path) ? TerrainCorpusTests.Sha256(path) : "missing";
                pinnedFiles++;
                if (sha != f.Value)
                {
                    changedFiles++;
                    firstChanged = firstChanged ?? f.Key;
                }
            }
            Check(pinnedFiles == 21 && changedFiles == 0,
                  "fixtures: every file the manifest pins (" + pinnedFiles + ") has its SHA-256" + (firstChanged != null ? "; first changed " + firstChanged : ""));
            if (changedFiles != 0)
                return;

            var sets = new Dictionary<string, WorldLightSet>();
            foreach (string c in Clients)
                foreach (int m in new[] { 0, 1 })
                {
                    string path = Path.Combine(folder, c + "_map" + m + ".json");
                    if (!File.Exists(path))
                    {
                        Check(false, "fixture " + path + " exists");
                        continue;
                    }
                    WorldLightSet s;
                    using (JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(path)))
                        s = FromJson(doc.RootElement);
                    sets[c + "/" + m] = s;
                    Check(s.State == "ok" && s.DroppedZones + s.DroppedPoints + s.DroppedLights + s.DroppedParams + s.DroppedDays == 0 && s.UnknownColumns == 0 &&
                          s.Map == m,
                          "fixture " + c + " map " + m + " read whole: " + s.Summary());

                    // Byte parity with the host's sample() for every light's slot-0 day, at 1440 and nine more half-minutes.
                    string parity = Path.Combine(folder, "parity_" + c + "_map" + m + ".json");
                    int rows = 0, mismatches = 0, at1440 = 0;
                    string first = null;
                    using (JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(parity)))
                        foreach (JsonElement row in doc.RootElement.EnumerateArray())
                        {
                            rows++;
                            int light = Get(row, "light").GetInt32(), t = Get(row, "t").GetInt32();
                            bool fb;
                            WorldLightParams p = s.ParamsOf(light, 0, out fb);
                            bool ok = p != null && !fb && p.Id == Get(row, "params").GetInt32();
                            for (int k = 0; ok && k < SampledNames.Length; k++)
                            {
                                int[] want = Ints(Get(row, SampledNames[k]));
                                int r, g, b;
                                s.DayAt(p.Day).Colour(t, SampledColumns[k], false, out r, out g, out b);
                                ok = r == want[0] && g == want[1] && b == want[2];
                            }
                            if (t == 1440) at1440++;
                            if (!ok)
                            {
                                mismatches++;
                                if (first == null) first = "light " + light + " t " + t;
                            }
                        }
                    Check(rows > 0 && at1440 > 0 && mismatches == 0,
                          "parity with the generator's offline port of the host's sample() (not the host itself), " + c + " map " + m + ": " + rows +
                          " light-times (" + at1440 + " at 1440), river/ocean close/far and SkyBand1 byte-identical" +
                          (first != null ? "; first mismatch " + first : ""));
                }

            // The survey's spot dumps (spots.json): the selection and every column of every light at 00:00, 06:00, 12:00, 18:00.
            string spots = Path.Combine(folder, "spots.json");
            if (!File.Exists(spots))
            {
                Check(false, "spots.json exists in the fixture folder");
                return;
            }
            var ev = new WorldLightEvaluator();
            var smp = new WorldLightSample();
            var sel = new WorldLightSelection();
            var rgb = new int[3 * WorldLightColumns.ColourCount];
            var fl = new double[WorldLightColumns.FloatCount];
            var ints = new long[WorldLightColumns.IntCount];
            int[] times = { 0, 720, 1440, 2160 };
            using (JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(spots)))
                foreach (JsonProperty sp in doc.RootElement.EnumerateObject())
                {
                    JsonElement o = sp.Value;
                    string client = Get(o, "client").GetString();
                    int map = Get(o, "map").GetInt32();
                    WorldLightSet s;
                    if (!sets.TryGetValue(client + "/" + map, out s))
                        continue;
                    double x = Get(o, "x").GetDouble(), y = Get(o, "y").GetDouble(), z = Get(o, "z").GetDouble();
                    s.Select(x, y, z, 0, new WorldLightRules(), sel);
                    JsonElement zone = Get(o, "zone");
                    int wantZone = zone.ValueKind == JsonValueKind.Array ? zone[0].GetInt32() : (Get(o, "default").GetInt32() > 0 ? 0 : -1);
                    // The survey lists every point light that weighs there; the player applies those whose LightParams have keys.
                    var wantPoints = new List<KeyValuePair<int, double>>();
                    foreach (JsonElement p in Get(o, "points").EnumerateArray())
                    {
                        bool fb;
                        if (s.ParamsOf(Get(p, "light").GetInt32(), 0, out fb) != null)
                            wantPoints.Add(new KeyValuePair<int, double>(Get(p, "light").GetInt32(), Get(p, "w").GetDouble()));
                    }
                    bool points = wantPoints.Count == sel.Points.Count;
                    for (int k = 0; points && k < wantPoints.Count; k++)
                        points = sel.Points[k].Light == wantPoints[k].Key && Near(sel.Points[k].W, wantPoints[k].Value, 1e-12);
                    Check(sel.Light == Get(o, "base").GetInt32() && sel.Zone == wantZone && points,
                          "spot " + sp.Name + ": base Light " + sel.Light + ", zone " + sel.Zone + ", " + sel.Points.Count + " point light(s) as the survey's dump " +
                          "(Stage 09's rules)");

                    // Every light's day at the four times, every column the dump and the player share.
                    int compared = 0, differ = 0;
                    string firstDiff = null;
                    foreach (JsonProperty lp in Get(o, "lights").EnumerateObject())
                    {
                        int light = int.Parse(lp.Name, CultureInfo.InvariantCulture);
                        bool fb;
                        WorldLightParams p = s.ParamsOf(light, 0, out fb);
                        if (p == null || p.Id != Get(lp.Value, "params").GetInt32())
                        {
                            differ++;
                            firstDiff = firstDiff ?? "Light " + light + ": no params " + Get(lp.Value, "params").GetInt32();
                            continue;
                        }
                        WorldLightDay day = s.DayAt(p.Day);
                        int[] keys = Ints(Get(lp.Value, "keys"));
                        if (keys.Length != day.Keys || !System.Linq.Enumerable.SequenceEqual(keys, day.Times))
                        {
                            differ++;
                            firstDiff = firstDiff ?? "Light " + light + ": key times";
                        }
                        for (int ti = 0; ti < times.Length; ti++)
                        {
                            day.Evaluate(times[ti], false, rgb, fl, ints);
                            foreach (JsonProperty col in Get(lp.Value, "values").EnumerateObject())
                            {
                                string name = col.Name.Replace("[", "").Replace("]", "");
                                JsonElement v = col.Value[ti];
                                int ci = Array.IndexOf(WorldLightColumns.Colours, name), fi = Array.IndexOf(WorldLightColumns.Floats, name),
                                    ii = Array.IndexOf(WorldLightColumns.Ints, name);
                                bool ok;
                                if (ci >= 0)
                                    ok = rgb[3 * ci] == v[0].GetInt32() && rgb[3 * ci + 1] == v[1].GetInt32() && rgb[3 * ci + 2] == v[2].GetInt32();
                                else if (fi >= 0)
                                    ok = fl[fi] == v.GetDouble();
                                else if (ii >= 0)
                                    ok = ints[ii] == v.GetInt64();
                                else
                                    continue;   // a column the player does not carry (Field_*)
                                compared++;
                                if (!ok)
                                {
                                    differ++;
                                    firstDiff = firstDiff ?? "Light " + light + " " + col.Name + " at " + times[ti];
                                }
                            }
                        }
                    }
                    Check(compared > 0 && differ == 0,
                          "spot " + sp.Name + ": " + compared + " light-column-times equal the survey's dump exactly (colours as the host rounds, floats " +
                          "linear, integers the earlier key's)" + (firstDiff != null ? "; first difference " + firstDiff : ""));
                }

            // Maps 0 and 1 have no time of day override in any client: Retail's -1, the Classic layouts' 16-bit -1 (65535 unextended).
            int overridden = 0;
            var raw = new List<string>();
            foreach (KeyValuePair<string, WorldLightSet> kv in sets)
            {
                if (kv.Value.TimeOverrideMinutes >= 0) overridden++;
                raw.Add(kv.Key + " " + kv.Value.TimeOverride);
            }
            Check(overridden == 0 && sets.Count == 10,
                  "maps 0 and 1 carry no time override in any client (as sent: " + string.Join(", ", raw.ToArray()) + ")");
        }

        /// <summary>The host's own objects (*.json: the object, or a load line carrying "worldLight"): each read through the same
        /// path, nothing dropped, its days evaluated without fault; beside it, the host's Stage 09 "lights" object of the same
        /// client and map (the same name with "worldlight" replaced by "lights", or a load line's "liquids.lights"), whose values
        /// are the host's own LightCatalog::sample() at its time and slot 0: every value row byte-identical to the player's day of
        /// that light at that time, and every light with a slot-0 day whose water colours there are not all 0 has a row (Stage 09's
        /// rule); and -- where the fixtures hold the same client and map -- every light's slot-0 colours at 1440 equal the
        /// fixture's (the offline reading of the same bytes).</summary>
        static void HostDumpTests(string folder, string fixtures)
        {
            string[] files = Directory.GetFiles(folder, "*.json", SearchOption.AllDirectories);
            int read = 0;
            foreach (string path in files)
            {
                JsonDocument doc;
                try { doc = JsonDocument.Parse(File.ReadAllBytes(path)); }
                catch (Exception e) { Check(false, "host dump " + path + " parses: " + e.Message); continue; }
                using (doc)
                {
                    JsonElement o = doc.RootElement;
                    JsonElement lightsInLine = default(JsonElement);
                    if (Has(o, "worldLight"))
                    {
                        JsonElement liquids = Get(o, "liquids");
                        if (Has(liquids, "lights"))
                            lightsInLine = Get(liquids, "lights");
                        o = Get(o, "worldLight");
                    }
                    if (!Has(o, "state") || (Get(o, "state").GetString() == "ok" && !Has(o, "days")))
                        continue;   // not an outdoor light object (a Stage 09 "lights" object has no days)
                    read++;
                    WorldLightSet s = FromJson(o);
                    if (s.State != "ok")
                    {
                        Check(s.State == "unavailable", "host dump " + Path.GetFileName(path) + ": state " + s.State + " (" + s.Detail + ")");
                        continue;
                    }
                    var rgb = new int[3 * WorldLightColumns.ColourCount];
                    var fl = new double[WorldLightColumns.FloatCount];
                    var ints = new long[WorldLightColumns.IntCount];
                    int days = 0;
                    for (int k = 0; k < s.DayCount; k++)
                        if (s.DayAt(k) != null)
                        {
                            s.DayAt(k).Evaluate(1440, false, rgb, fl, ints);
                            days++;
                        }
                    string name = Path.GetFileName(Path.GetDirectoryName(path)) + "/" + Path.GetFileName(path);
                    Check(s.DroppedZones + s.DroppedPoints + s.DroppedLights + s.DroppedParams + s.DroppedDays == 0 && days == s.DayCount && s.UnknownColumns == 0,
                          "host dump " + name + " read whole: " + s.Summary());

                    // The host's own sample(): its Stage 09 "lights" object of the same map.
                    string sibling = Path.Combine(Path.GetDirectoryName(path), Path.GetFileName(path).Replace("worldlight", "lights"));
                    JsonDocument ld = null;
                    if (lightsInLine.ValueKind != JsonValueKind.Object && sibling != path && File.Exists(sibling))
                    {
                        ld = JsonDocument.Parse(File.ReadAllBytes(sibling));
                        lightsInLine = ld.RootElement;
                    }
                    if (lightsInLine.ValueKind == JsonValueKind.Object && Has(lightsInLine, "values"))
                    {
                        int t = Has(lightsInLine, "time") ? Get(lightsInLine, "time").GetInt32() : 1440;
                        int rows = 0, mismatches = 0, missing = 0;
                        string first = null;
                        var listed = new HashSet<int>();
                        foreach (JsonElement row in Get(lightsInLine, "values").EnumerateArray())
                        {
                            rows++;
                            int light = Get(row, "light").GetInt32();
                            listed.Add(light);
                            bool fb;
                            WorldLightParams p = s.ParamsOf(light, 0, out fb);
                            bool ok = p != null && !fb && p.Id == Get(row, "lightParams").GetInt32();
                            for (int k = 0; ok && k < SampledNames.Length; k++)
                            {
                                if (!Has(row, SampledNames[k]))
                                    continue;   // ("sky" is optional in Stage 09's rows)
                                int[] want = Ints(Get(row, SampledNames[k]));
                                int r, g, b;
                                s.DayAt(p.Day).Colour(t, SampledColumns[k], false, out r, out g, out b);
                                ok = r == want[0] && g == want[1] && b == want[2];
                            }
                            if (!ok)
                            {
                                mismatches++;
                                first = first ?? "Light " + light;
                            }
                        }
                        // Every light the object names whose slot-0 day has water colours at t has a row (Stage 09's rule).
                        var named = new HashSet<int>();
                        foreach (WorldLightZone z in s.Zones) named.Add(z.Light);
                        foreach (WorldLightPoint pt in s.Points) named.Add(pt.Light);
                        if (s.DefaultLight > 0) named.Add(s.DefaultLight);
                        foreach (int id in named)
                        {
                            bool fb;
                            WorldLightParams p = s.ParamsOf(id, 0, out fb);
                            if (p == null || fb)
                                continue;
                            s.DayAt(p.Day).Evaluate(t, false, rgb, null, null);
                            bool water = false;
                            foreach (int c in new[] { WorldLightColumns.RiverCloseColor, WorldLightColumns.RiverFarColor, WorldLightColumns.OceanCloseColor,
                                                      WorldLightColumns.OceanFarColor })
                                water |= rgb[3 * c] != 0 || rgb[3 * c + 1] != 0 || rgb[3 * c + 2] != 0;
                            if (water && !listed.Contains(id))
                            {
                                missing++;
                                first = first ?? "Light " + id + " has no row";
                            }
                        }
                        Check(rows > 0 && mismatches == 0 && missing == 0,
                              "host sample() parity, " + name + ": " + rows + " value rows of the host's own Stage 09 object (time " + t + ", slot 0) " +
                              "byte-identical to the player's days (river/ocean close/far, sky); every light with water colours there has its row" +
                              (first != null ? "; first difference " + first : ""));
                    }
                    if (ld != null)
                        ld.Dispose();

                    string fixture = fixtures != null ? Path.Combine(fixtures, s.Product + "_map" + s.Map + ".json") : null;
                    if (fixture == null || !File.Exists(fixture))
                        continue;
                    WorldLightSet f;
                    using (JsonDocument fd = JsonDocument.Parse(File.ReadAllBytes(fixture)))
                        f = FromJson(fd.RootElement);
                    // Every light's slots 0 and 1, their LightParams fields and days: every colour, float (as float32) and integer at
                    // six half-minutes, against the offline reading of the same bytes.
                    int lights = 0, differ = 0;
                    string firstDiff = null;
                    var frgb = new int[rgb.Length];
                    var ffl = new double[fl.Length];
                    var fints = new long[ints.Length];
                    var ids = new HashSet<int>();
                    foreach (WorldLightZone z in f.Zones) ids.Add(z.Light);
                    foreach (WorldLightPoint p in f.Points) ids.Add(p.Light);
                    if (f.DefaultLight > 0) ids.Add(f.DefaultLight);
                    foreach (int id in ids)
                        foreach (int slot in new[] { 0, 1 })
                        {
                            bool fb1, fb2;
                            WorldLightParams a = s.ParamsOf(id, slot, out fb1), b = f.ParamsOf(id, slot, out fb2);
                            if (a == null && b == null)
                                continue;
                            lights++;
                            if (a == null || b == null || a.Id != b.Id || fb1 != fb2 || a.Glow != b.Glow || a.Skybox != b.Skybox || a.Flags != b.Flags ||
                                a.HighlightSky != b.HighlightSky || a.SunPolar != b.SunPolar || a.SunAzimuth != b.SunAzimuth ||
                                !System.Linq.Enumerable.SequenceEqual(a.OverrideSun, b.OverrideSun))
                            {
                                differ++;
                                firstDiff = firstDiff ?? "Light " + id + " slot " + slot + ": LightParams";
                                continue;
                            }
                            if (!System.Linq.Enumerable.SequenceEqual(s.DayAt(a.Day).Times, f.DayAt(b.Day).Times))
                            {
                                differ++;
                                firstDiff = firstDiff ?? "Light " + id + " slot " + slot + ": key times";
                                continue;
                            }
                            foreach (int t in new[] { 0, 1, 720, 1440, 2159, 2879 })
                            {
                                s.DayAt(a.Day).Evaluate(t, false, rgb, fl, ints);
                                f.DayAt(b.Day).Evaluate(t, false, frgb, ffl, fints);
                                if (!System.Linq.Enumerable.SequenceEqual(rgb, frgb) || !System.Linq.Enumerable.SequenceEqual(fl, ffl) ||
                                    !System.Linq.Enumerable.SequenceEqual(ints, fints))
                                {
                                    differ++;
                                    firstDiff = firstDiff ?? "Light " + id + " slot " + slot + ": columns at " + t;
                                    break;
                                }
                            }
                        }
                    var zs = new List<int>();
                    var fzs = new List<int>();
                    foreach (WorldLightZone z in s.Zones) zs.Add(z.Zone);
                    foreach (WorldLightZone z in f.Zones) fzs.Add(z.Zone);
                    Check(differ == 0 && System.Linq.Enumerable.SequenceEqual(zs, fzs) && s.Points.Count == f.Points.Count && s.DefaultLight == f.DefaultLight &&
                          s.DefaultFrom == f.DefaultFrom && s.TimeOverrideMinutes == f.TimeOverrideMinutes && s.Branch == f.Branch &&
                          s.LightDataLayout == f.LightDataLayout && s.MapFlags[0] == f.MapFlags[0] && s.MapFlags[1] == f.MapFlags[1] && s.MapFlags[2] == f.MapFlags[2],
                          "host dump " + name + " against the offline fixture: the zones in the same order (" + zs.Count + "), " + s.Points.Count + " points, the " +
                          "default light, branch, layout, map flags and override; " + lights + " light-slots' LightParams and every colour, float and integer " +
                          "of their days at six half-minutes equal" + (firstDiff != null ? "; first difference " + firstDiff : ""));
                }
            }
            Check(read > 0, "host dumps: " + read + " object(s) read from " + folder);
        }

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();
            log.Add("World light: the shared geometry");
            GeometryTests();
            log.Add("World light: the day");
            DayTests();
            log.Add("World light: the selection");
            SelectionTests();
            log.Add("World light: the sample");
            SampleTests();
            log.Add("World light: the clock");
            ClockTests();
            log.Add("World light: the ease");
            EaseTests();
            log.Add("World light: the switches");
            SwitchTests();
            log.Add("World light: the graded model");
            ModelTests();
            string fixtures = Environment.GetEnvironmentVariable("WMV_WORLD_LIGHT_FIXTURES");
            log.Add("World light: the offline fixtures (maps 0 and 1 of five clients)");
            if (string.IsNullOrEmpty(fixtures))
                log.Add("  SKIPPED: WMV_WORLD_LIGHT_FIXTURES is not set (the fixtures are generated locally, not committed)");
            else
                FixtureTests(fixtures);
            string dumps = Environment.GetEnvironmentVariable("WMV_WORLD_LIGHT_HOST_DUMPS");
            log.Add("World light: the host's own objects");
            if (string.IsNullOrEmpty(dumps))
                log.Add("  SKIPPED: WMV_WORLD_LIGHT_HOST_DUMPS is not set");
            else
                HostDumpTests(dumps, fixtures);
            log.Add(string.Format("WorldLightTests: {0} failure(s)", failures));
            if (output != null)
                foreach (string line in log)
                    output(line);
            return failures;
        }
    }
}
