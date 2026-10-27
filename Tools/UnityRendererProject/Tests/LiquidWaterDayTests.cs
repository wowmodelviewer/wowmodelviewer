// LiquidWaterDayTests.cs
//
// Unit tests of Stage 10 step 8's pure parts (docs/world-viewer/10-light-sky-fog.md 6.11, 13.3): the water's values on the
// light's clock (Wow/World/LiquidWaterDay.cs -- the rounding and interpolation between keys, the wrap past midnight, the clamp
// arm, a one-key day, a light without water colours for a span, a point light skipped while it has none, the noon fall-backs,
// the cache, no allocation in the steady state), the ease's two step-8 snaps (LiquidLightEase: a clock jump, a change of
// source; Stage 09's reasons unchanged), the sky dome's wedge twin (SkyDome.TraceWedge against SkyDome.Trace on both tables),
// the CPU twin of the fogged water (LiquidHighMath.MixFogged, FogPremultiplied), the water switches and their texts; and, when
// WMV_WORLD_LIGHT_HOST_DUMPS names the host's own dumps (D:\wmv-10\step3\host\dumps: the Stage 09 "lights" object and the
// protocol-20 "worldLight" object of maps 0 and 1 of five clients), THE 12:00 INVARIANT -- the table at half-minute 1440 equal
// to Stage 09's values entry for entry, and the selection on it equal to Stage 09's at every zone's centroid and vertices,
// every point light's position and falloff edges and a point no zone holds -- and the time sweep (every 30 minutes: the
// validity never changes on maps 0 and 1, the colours are the day's). Without it that part reports SKIPPED.
// Framework-free like the other suites: RunAll returns the failure count.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class LiquidWaterDayTests
    {
        static int failures;
        static readonly List<string> log = new List<string>();

        static void Check(bool condition, string name)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name); failures++; }
        }

        static float[] Rgb(float r, float g, float b) { return new[] { r, g, b }; }

        static int Hex(int r, int g, int b) { return (r << 16) | (g << 8) | b; }

        static readonly string[] WaterColumns = { "RiverCloseColor", "RiverFarColor", "OceanCloseColor", "OceanFarColor" };

        /// <summary>A set whose days carry the four water colours only; each day's keys give the same colour to all four columns
        /// (scaled per column by 1, 0.5, 0.25, 0.75 so the columns differ).</summary>
        static WorldLightSet WaterSet()
        {
            var s = new WorldLightSet { State = "ok", Map = 0, Product = "test" };
            s.SetColumns(WaterColumns, new string[0], new string[0], new string[0]);
            return s;
        }

        static int Scaled(int c, int column)
        {
            double f = column == 0 ? 1.0 : column == 1 ? 0.5 : column == 2 ? 0.25 : 0.75;
            int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
            return Hex((int)(r * f), (int)(g * f), (int)(b * f));
        }

        /// <summary>A light `light` with LightParams `paramsId` whose slot-0 day has keys `times` of colours `colours` (0xRRGGBB).</summary>
        static void AddLight(WorldLightSet s, int light, int paramsId, int[] times, int[] colours)
        {
            var c = new int[times.Length * 4];
            for (int k = 0; k < times.Length; k++)
                for (int j = 0; j < 4; j++)
                    c[4 * k + j] = Scaled(colours[k], j);
            int day = s.AddDay(times, c, new double[0], new long[0]);
            s.AddParams(paramsId, day, 0, 0, 0, 0, null, 0, 0);
            s.AddLight(light, new[] { paramsId, 0, 0, 0, 0, 0, 0, 0 });
        }

        /// <summary>The Stage 09 row of a light: its colours at noon as the host sends them (bytes), alphas, a sky.</summary>
        static void AddRow(LiquidLights l, WorldLightSet s, int light, int paramsId, float alpha, bool sky)
        {
            bool fb;
            WorldLightParams p = s.ParamsOf(light, 0, out fb);
            var cols = new float[4][];
            for (int j = 0; j < 4; j++)
            {
                int r = 0, g = 0, b = 0;
                if (p != null)
                    s.DayAt(p.Day).Colour(LiquidWaterDay.Noon, WorldLightColumnsOf(j), false, out r, out g, out b);
                cols[j] = Rgb(r, g, b);
            }
            l.AddValues(light, paramsId, cols[0], cols[1], cols[2], cols[3], new[] { alpha, alpha + 0.1f }, new[] { alpha + 0.2f, alpha + 0.3f },
                        sky ? Rgb(10, 20, 30) : null);
        }

        static int WorldLightColumnsOf(int j)
        {
            return j == 0 ? WorldLightColumns.RiverCloseColor : j == 1 ? WorldLightColumns.RiverFarColor : j == 2 ? WorldLightColumns.OceanCloseColor
                 : WorldLightColumns.OceanFarColor;
        }

        static double[] Square(double x0, double y0, double x1, double y1)
        {
            return new[] { x0, y0, x1, y0, x1, y1, x0, y1 };
        }

        static bool SameSample(LiquidLightSample a, LiquidLightSample b)
        {
            if (a.Valid != b.Valid || a.Light != b.Light || a.Zone != b.Zone || a.LightParams != b.LightParams || a.Points != b.Points || a.Why != b.Why ||
                a.HasSky != b.HasSky)
                return false;
            for (int k = 0; k < LiquidLightSample.Count; k++)
                if (BitConverter.DoubleToInt64Bits(a.V[k]) != BitConverter.DoubleToInt64Bits(b.V[k]))
                    return false;
            return true;
        }

        static bool SameValues(LiquidLightValues a, LiquidLightValues b)
        {
            if (a.Light != b.Light || a.LightParams != b.LightParams || a.HasSky != b.HasSky)
                return false;
            for (int k = 0; k < LiquidLightSample.Count; k++)
                if (BitConverter.DoubleToInt64Bits(a.V[k]) != BitConverter.DoubleToInt64Bits(b.V[k]))
                    return false;
            return true;
        }

        // ================================================================================== the table on the clock

        static void TableTests()
        {
            WorldLightSet s = WaterSet();
            int a = Hex(40, 100, 201), b = Hex(81, 50, 0);
            AddLight(s, 10, 110, new[] { 720, 2160 }, new[] { a, b });              // the base light: two keys
            AddLight(s, 20, 120, new[] { 480, 2400 }, new[] { b, a });              // keys either side of midnight
            AddLight(s, 30, 130, new[] { 1000 }, new[] { Hex(9, 8, 7) });           // one key
            AddLight(s, 40, 140, new[] { 1440, 2160, 2400 }, new[] { a, 0, 0 });    // all four colours 0 from 18:00 to 20:00
            AddLight(s, 50, 150, new[] { 1440, 2160, 2400 }, new[] { b, 0, 0 });    // a point light, 0 from 18:00 to 20:00
            AddLight(s, 60, 999, new[] { 1440 }, new[] { a });                      // its row names LightParams 160: another slot-0 one
            var l = new LiquidLights { Map = 0, Time = 1440, DefaultLight = 10 };
            l.AddZone(1, 40, -100, 100, 0, Square(100, 100, 200, 200));
            l.AddZone(2, 60, -100, 100, 0, Square(300, 300, 400, 400));
            l.AddPoint(50, 0, 0, 0, 10, 20);
            AddRow(l, s, 10, 110, 0.4f, true);
            AddRow(l, s, 20, 120, 0.5f, false);
            AddRow(l, s, 30, 130, 0.6f, true);
            AddRow(l, s, 40, 140, 0.7f, false);
            AddRow(l, s, 50, 150, 0.2f, true);
            AddRow(l, s, 60, 160, 0.3f, false);
            l.AddValues(70, 170, Rgb(1, 2, 3), Rgb(4, 5, 6), Rgb(7, 8, 9), Rgb(1, 1, 1), new[] { 0.1f, 0.2f }, new[] { 0.3f, 0.4f });   // no day in the set

            var w = new LiquidWaterDay();
            bool built = w.Update(l, s, 1440, false);
            int byte1440 = WorldLightDay.Round(40, 81, 0.5);                       // the base light half-way: floor(40 + 41 x 0.5 + 0.5) = 61
            LiquidLightValues v10 = w.Table[10], v20 = w.Table[20];
            bool noon = built && w.Table.Count == 7 && w.Rows == 7 && w.FromDay == 5 && w.NoonFallbacks == 2 && w.ParamsMismatches == 1 && w.ZeroAtTime == 0;
            LiquidLightValues row10, row70, row60;
            l.TryGetValues(10, out row10);
            l.TryGetValues(70, out row70);
            l.TryGetValues(60, out row60);
            bool rows = SameValues(v10, row10) && ReferenceEquals(w.Table[70], row70) && ReferenceEquals(w.Table[60], row60) &&
                        v10.V[LiquidLightSample.RiverClose] == byte1440 / 255.0 && v10.V[LiquidLightSample.WaterAlpha] == (double)0.4f &&
                        v10.HasSky && v10.V[LiquidLightSample.Sky] == 10 / 255.0 && !v20.HasSky;
            Check(noon && rows, "table at 12:00: every row light has an entry; the colours of its slot-0 day at 1440 (half-way between 06:00 and 18:00: " +
                                "R " + byte1440 + ", the host's rounding) equal its row bit for bit, the alphas and the sky the row's; a light whose day the set " +
                                "lacks (70) and one whose slot-0 LightParams is not the row's (60) keep their rows, counted (" + w.Summary() + ")");

            // Interpolation and rounding between keys; the wrap past midnight; the clamp arm; a one-key day.
            w.Update(l, s, 1080, false);                                             // 09:00: a quarter of the way from 06:00 to 18:00
            int q = WorldLightDay.Round(100, 50, 0.25);
            bool quarter = w.Table[10].V[LiquidLightSample.RiverClose + 1] == q / 255.0 &&
                           w.Table[10].V[LiquidLightSample.RiverFar + 1] == WorldLightDay.Round(50, 25, 0.25) / 255.0;
            w.Update(l, s, 0, false);                                                // midnight: half-way from 20:00 to 04:00 next day
            bool wrap = w.Table[20].V[LiquidLightSample.RiverClose] == WorldLightDay.Round(40, 81, 0.5) / 255.0;
            w.Update(l, s, 0, true);                                                 // clamped: before the first key, the first
            bool clamped = w.Table[20].V[LiquidLightSample.RiverClose] == 81 / 255.0 && w.Clamp;
            w.Update(l, s, 2879, false);
            bool oneKey = w.Table[30].V[LiquidLightSample.OceanClose] == (9 / 4) / 255.0 && w.Table[30].V[LiquidLightSample.RiverClose] == 9 / 255.0;
            Check(quarter && wrap && clamped && oneKey,
                  "table on the clock: 09:00 a quarter of the way (G " + q + "), each column its own; 00:00 between 20:00 and 04:00 across midnight; " +
                  "the clamp arm takes the first key before it; a one-key day holds all day");

            // A light without water colours for a span: invalid there (the type colours), valid around it; a point light skipped.
            var sample = new LiquidLightSample();
            w.Update(l, s, 2200, false);
            w.Evaluate(150, 150, 0, sample);
            bool invalidThere = !sample.Valid && sample.Light == 40 && sample.Why == LiquidLightSample.WhyNoValues && w.ZeroAtTime == 2 &&
                                !w.Table.ContainsKey(40) && !w.Table.ContainsKey(50);
            w.Evaluate(5, 0, 0, sample);
            bool pointSkipped = sample.Valid && sample.Light == 10 && sample.Points == 0;
            w.Update(l, s, 2100, false);
            w.Evaluate(150, 150, 0, sample);
            bool validAround = sample.Valid && sample.Light == 40;
            w.Evaluate(5, 0, 0, sample);
            bool pointApplied = sample.Valid && sample.Points == 1;
            Check(invalidThere && pointSkipped && validAround && pointApplied,
                  "table on the clock: a base light whose four colours are 0 at t (18:20) has no entry -- invalid there, the type colours (" +
                  LiquidLightSample.WhyNoValues + ") --, valid at 17:30; a point light without colours at t is skipped, applied at 17:30");

            // The cache: the same inputs build nothing; any change of the four builds again.
            int builds = w.Builds;
            bool same = !w.Update(l, s, 2100, false) && !w.Update(l, s, 2100 + WorldLightDay.Day, false) && w.Builds == builds;
            bool changed = w.Update(l, s, 2101, false) && w.Update(l, s, 2101, true) && w.Update(l, WaterSet(), 2101, true) && w.Builds == builds + 3;
            w.Update(l, s, 1440, false);
            Check(same && changed && w.Table.Count == 7, "table cache: the same lights, set, half-minute (also one day later) and clamp build nothing; " +
                                                         "a change of any of them builds again");

            // No allocation in the steady state: frames on the same inputs, and a new half-minute once every light's values exist.
            for (int k = 0; k < 4; k++)
            {
                w.Update(l, s, 1440 + k, false);
                w.Evaluate(5, 0, 0, sample);
            }
            long before = GC.GetAllocatedBytesForCurrentThread();
            for (int k = 0; k < 1000; k++)
            {
                w.Update(l, s, 1440 + (k & 3), false);
                w.Evaluate(5, 0, 0, sample);
                w.Evaluate(150, 150, 0, sample);
            }
            long allocated = GC.GetAllocatedBytesForCurrentThread() - before;
            Check(allocated == 0, "table: 1000 frames (each a new half-minute, a rebuild) and 2000 selections allocate nothing (" + allocated + " bytes)");

            // Stage 09's path is the table overload on the host's own rows.
            var s09 = new LiquidLightSample();
            bool overload = true;
            foreach (double[] at in new[] { new double[] { 150, 150, 0 }, new double[] { 5, 0, 0 }, new double[] { 350, 350, 0 }, new double[] { 900, 900, 0 } })
            {
                l.Evaluate(at[0], at[1], at[2], s09);
                w.Update(l, s, LiquidWaterDay.Noon, false);
                w.Evaluate(at[0], at[1], at[2], sample);
                overload &= SameSample(s09, sample);
            }
            Check(overload, "table at 12:00: the selection on it equals Stage 09's at a zone, a point light, a fall-back light's zone and the default");

            // A set that lacks a water colour column (its layout's absentColumns): every row light keeps its noon row, counted and said,
            // instead of a column read as 0 in every key making lights invalid that Stage 09 shows.
            var sa = new WorldLightSet { State = "ok", Map = 0, Product = "test" };
            sa.SetColumns(WaterColumns, new string[0], new string[0], new[] { "OceanFarColor" });
            AddLight(sa, 10, 110, new[] { 720, 2160 }, new[] { a, b });
            AddLight(sa, 40, 140, new[] { 1440, 2160, 2400 }, new[] { a, 0, 0 });
            var wa = new LiquidWaterDay();
            wa.Update(l, sa, 2200, false);
            bool allRows = wa.Table.Count == 7 && wa.FromDay == 0 && wa.NoonFallbacks == 7 && wa.ZeroAtTime == 0 && wa.AbsentColumns == "OceanFarColor";
            foreach (LiquidLightValues row in l.Rows)
            {
                LiquidLightValues e;
                allRows &= wa.Table.TryGetValue(row.Light, out e) && ReferenceEquals(e, row);
            }
            wa.Evaluate(150, 150, 0, sample);
            bool stillValid = sample.Valid && sample.Light == 40;
            string absentSummary = wa.Summary();
            wa.Update(l, s, 2200, false);
            bool back = wa.AbsentColumns == null && wa.FromDay == 3 && wa.ZeroAtTime == 2 && wa.NoonFallbacks == 2;
            Check(allRows && stillValid && back && absentSummary.EndsWith("; water colour columns absent: OceanFarColor", StringComparison.Ordinal),
                  "table: a worldLight object without a water colour column (OceanFarColor absent) keeps every row light at noon (7 of 7, said: " +
                  absentSummary + ") -- light 40 stays valid at 18:20 --; the next object with all four reads the day again (" + wa.Summary() + ")");
            Check(LiquidWaterDay.Clock(1440) == "12:00" && LiquidWaterDay.Clock(1081) == "09:00:30" && LiquidWaterDay.Clock(-1) == "23:59:30",
                  "the log's clock text: hh:mm, :30 on an odd half-minute, wrapped into the day");
        }

        // ================================================================================== the ease

        static void EaseTests()
        {
            var t = new LiquidLightSample { Valid = true, Light = 1 };
            t.V[0] = 0.5;
            var e = new LiquidLightEase();
            var src = new object();
            string first = e.Step(t, src, 0, 0, 0, 0.016, false);
            t.V[0] = 1.0;
            string eased = e.Step(t, src, 0, 0, 0, 0.016, false);
            string clock = e.Step(t, src, 0, 0, 0, 0.016, false, true);
            double afterClock = e.V[0];
            string toClock = e.Step(t, src, 0, 0, 0, 0.016, false, false, true);
            t.V[0] = 0.25;
            string onClock = e.Step(t, src, 0, 0, 0, 0.016, false, false, true);
            string pinned = e.Step(t, src, 0, 0, 0, 0.016, true, true, true);
            string back = e.Step(t, src, 0, 0, 0, 0.016, false, false, false);
            Check(first == LiquidLightEase.SnapFirst && eased == null && clock == LiquidLightEase.SnapClock && afterClock == 1.0 &&
                  toClock == LiquidLightEase.SnapSource && onClock == null && pinned == LiquidLightEase.SnapPinned && back == LiquidLightEase.SnapSource &&
                  LiquidLightEase.SnapClock == WorldLightEase.SnapClock,
                  "ease (step 8): a clock jump snaps (\"clock\", after \"pinned\", as the outdoor light's ease); a change between the noon rows and the " +
                  "clock's table snaps (\"source\") both ways; on the clock it eases as before");
            var e9 = new LiquidLightEase();
            string a9 = e9.SnapReason(src, true, 0, 0, 0, false), b9 = e9.Step(t, src, 0, 0, 0, 0.016, false);
            string c9 = e9.SnapReason(new object(), true, 0, 0, 0, false), d9 = e9.SnapReason(src, false, 0, 0, 0, false);
            string f9 = e9.SnapReason(src, true, 100, 0, 0, false), g9 = e9.SnapReason(src, true, 0, 0, 0, false);
            Check(a9 == LiquidLightEase.SnapFirst && b9 == LiquidLightEase.SnapFirst && c9 == LiquidLightEase.SnapLights && d9 == LiquidLightEase.SnapValidity &&
                  f9 == LiquidLightEase.SnapJump && g9 == null && !e9.OnClock,
                  "ease (step 8): Stage 09's calls give Stage 09's reasons (first, lights, validity, jump; none) and never the step-8 ones");
            e9.Reset();
            Check(!e9.OnClock && !e9.Has, "ease: Reset forgets the source too");
        }

        // ================================================================================== the sky in a reflected direction

        static SkyInputs SkyFixture(double sunElevationDeg, double angle)
        {
            var s = new SkyInputs();
            double[] colours = { 0.10, 0.20, 0.55, 0.25, 0.40, 0.70, 0.60, 0.55, 0.50, 0.85, 0.65, 0.45, 0.80, 0.70, 0.60, 0.30, 0.25, 0.35 };
            Array.Copy(colours, s.Colours, colours.Length);
            s.Angle = angle;
            s.Tint[0] = 1.0;
            s.Tint[1] = 0.6;
            s.Tint[2] = 0.2;
            s.WeightAbove = 0.6;
            s.WeightBelow = 0.2;
            double e = sunElevationDeg * Math.PI / 180.0;
            s.Sun[0] = Math.Cos(e) * Math.Cos(0.3);
            s.Sun[1] = Math.Sin(e);
            s.Sun[2] = Math.Cos(e) * Math.Sin(0.3);
            return s;
        }

        static void SkyWedgeTests()
        {
            foreach (bool tableB in new[] { false, true })
            {
                var p = new float[3 * SkyDome.VertexCount];
                var slots = new float[SkyDome.VertexCount];
                var ix = new int[3 * SkyDome.TriangleCount];
                SkyDome.Build(tableB, p, slots, ix);
                var rad = new double[SkyDome.ProfilePoints];
                var hgt = new double[SkyDome.ProfilePoints];
                SkyDome.Profile(tableB, rad, hgt);
                double worst = 0.0;
                int n = 0, missed = 0, edges = 0;
                string at = "";
                foreach (SkyInputs s in new[] { SkyFixture(12, 0.2), SkyFixture(-8, 0.5), SkyFixture(40, 1.0) })
                {
                    var dirs = new List<double[]>();
                    for (int lo = 0; lo < 24 * 8; lo++)
                        for (int el = -89; el <= 89; el += 2)
                        {
                            double lon = lo * (2 * Math.PI / (24 * 8)), e = el * Math.PI / 180.0;
                            dirs.Add(new[] { Math.Cos(e) * Math.Cos(lon), Math.Sin(e), Math.Cos(e) * Math.Sin(lon) });
                        }
                    // The wedge edges exactly (every 15 deg; lo multiples of 8 above), just beside them, the poles and near them, and the
                    // profile points' own elevations (the rings).
                    for (int j = 0; j < 24; j++)
                        foreach (double off in new[] { -1e-12, 1e-12, -1e-7, 1e-7 })
                            for (int k = 0; k < SkyDome.ProfilePoints; k++)
                            {
                                double lon = j * (2 * Math.PI / 24) + off, e = SkyDome.ElevationDeg(rad[k], hgt[k]) * Math.PI / 180.0;
                                dirs.Add(new[] { Math.Cos(e) * Math.Cos(lon), Math.Sin(e), Math.Cos(e) * Math.Sin(lon) });
                                edges++;
                            }
                    dirs.Add(new[] { 0.0, 1.0, 0.0 });
                    dirs.Add(new[] { 0.0, -1.0, 0.0 });
                    dirs.Add(new[] { 1e-9, 1.0, -1e-9 });
                    dirs.Add(new[] { -1e-9, -1.0, 1e-9 });
                    dirs.Add(new[] { 3.0, 0.0, -4.0 });
                    foreach (double[] d in dirs)
                    {
                        double r0, g0, b0, r1, g1, b1;
                        bool t0 = SkyDome.Trace(p, slots, ix, d[0], d[1], d[2], s, out r0, out g0, out b0);
                        bool t1 = SkyDome.TraceWedge(rad, hgt, d[0], d[1], d[2], s, out r1, out g1, out b1);
                        n++;
                        if (!t0 || !t1)
                        {
                            missed++;
                            continue;
                        }
                        double err = Math.Max(Math.Abs(r0 - r1), Math.Max(Math.Abs(g0 - g1), Math.Abs(b0 - b1)));
                        if (err > worst)
                        {
                            worst = err;
                            at = string.Format(System.Globalization.CultureInfo.InvariantCulture, "({0:R}, {1:R}, {2:R})", d[0], d[1], d[2]);
                        }
                    }
                }
                Check(worst <= 1e-9 && missed == 0 && n > 50000,
                      string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                    "sky wedge twin, table {0}: SkyDome.TraceWedge (one 15-deg wedge, 10 triangles) equals SkyDome.Trace (240 triangles) over " +
                                    "{1} directions of three sky inputs ({2} at the wedge edges and the rings, the poles): worst channel {3:E2}{4} (1e-9); {5} " +
                                    "missed", tableB ? "b" : "a", n, edges, worst, at.Length > 0 ? " at " + at : "", missed));
            }
            double rr, gg, bb;
            bool zero = !SkyDome.TraceWedge(false, 0, 0, 0, SkyFixture(12, 0.2), out rr, out gg, out bb);
            Check(zero && SkyDome.WedgeOf(1, 0) == 0 && SkyDome.WedgeOf(0, 1) == 6 && SkyDome.WedgeOf(-1, 0) == 12 && SkyDome.WedgeOf(1, -1e-12) == 23,
                  "sky wedge twin: a zero direction gives none; the wedge of +x is 0, of +z 6, of -x 12, just below +x 23");
        }

        // ================================================================================== the fogged water's twin

        static void FogTwinTests()
        {
            const double W = 0.30, bed = 0.62, T = 0.4, sky = 0.8, rb = 0.25, rd = 0.05, fade = 0.7, cB = 0.33, aB = 0.6, cS = 0.21, aS = 0.9;
            double fogC = 0.55;
            bool ok = true;
            foreach (bool hit in new[] { false, true })
            {
                double s09 = LiquidHighMath.Mix(LiquidHighMath.Refracted(W, bed, T),
                                                hit ? sky + (LiquidHighMath.Reflected(cB, aB, sky) - sky) * fade : sky,
                                                hit ? sky + (LiquidHighMath.Reflected(cS, aS, sky) - sky) * fade : sky, rb, rd);
                foreach (int arm in new[] { LiquidHighMath.WaterFogOff, LiquidHighMath.WaterFogSurface, LiquidHighMath.WaterFogComposite })
                {
                    ok &= LiquidHighMath.MixFogged(W, bed, T, sky, rb, rd, hit, fade, cB, aB, cS, aS, 1.0, fogC, arm) == s09;      // vis 1: Stage 09's
                    ok &= LiquidHighMath.MixFogged(W, bed, T, sky, rb, rd, hit, fade, cB, aB, cS, aS, 1.5, double.NaN, arm) == s09;
                }
                ok &= LiquidHighMath.MixFogged(W, bed, T, sky, rb, rd, hit, fade, cB, aB, cS, aS, 0.3, fogC, LiquidHighMath.WaterFogOff) == s09;
                // vis 0: fogC wS + the scene terms; the decomposition sums to Stage 09's (before the saturate) at any vis.
                double sB, xB, sS, xS;
                LiquidHighMath.ReflectionShares(hit, fade, cB, aB, out sB, out xB);
                LiquidHighMath.ReflectionShares(hit, fade, cS, aS, out sS, out xS);
                double wS = LiquidHighMath.SurfaceWeight(T, rb, rd, sB, sS), r = rb + rd;
                double scene = bed * T * (1 - r) + rb * xB + rd * xS, surface = W * (1 - T) * (1 - r) + sky * (rb * sB + rd * sS);
                ok &= Math.Abs(LiquidHighMath.MixFogged(W, bed, T, sky, rb, rd, hit, fade, cB, aB, cS, aS, 0.0, fogC, LiquidHighMath.WaterFogSurface) -
                               (fogC * wS + scene)) < 1e-15;
                ok &= Math.Abs(scene + surface - s09) < 1e-15;
                double half = LiquidHighMath.MixFogged(W, bed, T, sky, rb, rd, hit, fade, cB, aB, cS, aS, 0.5, fogC, LiquidHighMath.WaterFogSurface);
                ok &= Math.Abs(half - (scene + 0.5 * surface + 0.5 * fogC * wS)) < 1e-15;
                double comp = LiquidHighMath.MixFogged(W, bed, T, sky, rb, rd, hit, fade, cB, aB, cS, aS, 0.5, fogC, LiquidHighMath.WaterFogComposite);
                ok &= Math.Abs(comp - (fogC + (s09 - fogC) * 0.5)) < 1e-15;
            }
            Check(ok, "fogged water twin: at visibility 1 (or more) every arm is Stage 09's output itself; off is Stage 09's at any visibility; at 0 " +
                      "the surface arm is fogC wS + the scene terms; the scene and surface terms sum to Stage 09's; composite is lerp(fogC, Stage 09, vis)");
            // The bed term never carries fog: T 1, no reflection -- the bed itself at any visibility and fog colour; the composite fogs it.
            bool bedClean = true;
            foreach (double vis in new[] { 0.0, 0.25, 0.9 })
                foreach (double f in new[] { 0.0, 1.0 })
                    bedClean &= LiquidHighMath.MixFogged(W, bed, 1.0, sky, 0, 0, false, 0, 0, 0, 0, 0, vis, f, LiquidHighMath.WaterFogSurface) == bed;
            bool hitClean = Math.Abs(LiquidHighMath.MixFogged(W, bed, 1.0, sky, 1.0, 0.0, true, 1.0, 0.4, 1.0, 0, 0, 0.0, 1.0, LiquidHighMath.WaterFogSurface) - 0.4) < 1e-15;
            double bed09 = LiquidHighMath.Mix(LiquidHighMath.Refracted(W, bed, 1.0), sky, sky, 0, 0);
            bool composite = LiquidHighMath.MixFogged(W, bed, 1.0, sky, 0, 0, false, 0, 0, 0, 0, 0, 0.25, 1.0, LiquidHighMath.WaterFogComposite) ==
                             LiquidHighMath.Saturate(1.0 + (bed09 - 1.0) * 0.25) && bed09 != LiquidHighMath.Saturate(1.0 + (bed09 - 1.0) * 0.25);
            Check(bedClean && hitClean && composite,
                  "fogged water twin: the refracted bed (T 1) and a fully covered reflected texel are never fogged by the surface arm (the scene " +
                  "copies are fogged at their own distance); the composite arm fogs them (the client's literal form)");
            double z = LiquidHighMath.FogPremultiplied(0.3, 0.6, 0.25, 0.8), o = LiquidHighMath.FogPremultiplied(0.5, 1.0, 0.25, 0.8);
            Check(Math.Abs(z - (0.25 * 0.3 + 0.8 * 0.6 * 0.75)) < 1e-15 && Math.Abs(o - (0.8 + (0.5 - 0.8) * 0.25)) < 1e-15 &&
                  LiquidHighMath.FogPremultiplied(0.3, 0.6, 1.0, double.NaN) == 0.3 && LiquidHighMath.FogPremultiplied(0.3, 0.6, 0.0, 0.8) == 0.8 * 0.6,
                  "fogged water twin: premultiplied paths (zone, fallback) take vis rgb + (1 - vis) fogC alpha, the alpha carried; at alpha 1 (an " +
                  "opaque liquid) that is lerp(fogC, rgb, vis); visibility 1 is the input itself");
        }

        // ================================================================================== the switches

        static void SwitchTests()
        {
            WorldLightSwitches d = WorldLightSwitches.Parse(new string[0], null);
            WorldLightSwitches arms = WorldLightSwitches.Parse(new[] { "-wmvWaterLight=noon", "-wmvWaterSky=stage09", "-wmvWaterFog=composite" }, "on");
            WorldLightSwitches off = WorldLightSwitches.Parse(new[] { "-wmvWaterFog=off" }, "on");
            WorldLightSwitches back = WorldLightSwitches.Parse(new[] { "-wmvWaterLight=noon", "-wmvWaterLight=clock", "-wmvWaterSky=stage09",
                                                                       "-wmvWaterSky=dome", "-wmvWaterFog=off", "-wmvWaterFog=surface" }, null);
            WorldLightSwitches bad = WorldLightSwitches.Parse(new[] { "-wmvWaterLight=dusk", "-wmvWaterSky=cube", "-wmvWaterFog=" }, null);
            string dd = d.DescribeWater(), ad = arms.DescribeWater(), od = off.DescribeWater();
            Check(d.WaterClock && d.WaterSkyDome && d.WaterFog == LiquidHighMath.WaterFogSurface && d.DrawDefaults && !arms.WaterClock && !arms.WaterSkyDome &&
                  arms.WaterFog == LiquidHighMath.WaterFogComposite && !arms.DrawDefaults && arms.Notes.Count == 0 && off.WaterFog == LiquidHighMath.WaterFogOff &&
                  !off.DrawDefaults && back.DrawDefaults && back.Notes.Count == 0 && bad.DrawDefaults && bad.Notes.Count == 3,
                  "water switches (step 8): clock, dome and surface by default (DrawDefaults); -wmvWaterLight=noon, -wmvWaterSky=stage09, " +
                  "-wmvWaterFog=composite|off each taken; the last of two values wins; a bad value a note (" + string.Join(" | ", bad.Notes) + ")");
            Check(dd == "water: the light's clock (the water colours of each light's day at its half-minute; Stage 09's selection, validity, alphas and " +
                        "constants), reflections that miss take the sky dome, the client's fog on its surface terms (the refracted and reflected scene not " +
                        "fogged twice)" &&
                  ad.Contains("Stage 09's noon values (-wmvWaterLight=noon, an isolation arm)") && ad.Contains("(-wmvWaterSky=stage09, an isolation arm)") &&
                  ad.Contains("-wmvWaterFog=composite: the client's literal form, an ARM not compared until the owner decides") &&
                  od.Contains("no water fog (-wmvWaterFog=off)") && d.DescribeObjects("wow").Contains("; exterior MOCV not drawn; " + dd + "; fog:") &&
                  d.DescribeObjects("wow").Contains("; interiors, interior groups' water, sky and particles unfogged") &&
                  off.DescribeObjects("wow").Contains("; interiors, water, sky and particles unfogged") &&
                  !d.DescribeObjects("wow").Contains("water as Stage 09"),
                  "water switches (step 8): the description names the default and each arm (\"" + dd + "\"); the fog clause leaves only interior " +
                  "groups' water unfogged, all water with -wmvWaterFog=off");
        }

        // ================================================================================== the 12:00 invariant on the host's dumps

        static JsonElement Get(JsonElement o, string name)
        {
            JsonElement v;
            return o.ValueKind == JsonValueKind.Object && o.TryGetProperty(name, out v) ? v : default(JsonElement);
        }

        static bool Has(JsonElement o, string name) { JsonElement v; return o.ValueKind == JsonValueKind.Object && o.TryGetProperty(name, out v); }

        static float[] Floats(JsonElement e)
        {
            if (e.ValueKind != JsonValueKind.Array)
                return null;
            var r = new float[e.GetArrayLength()];
            int k = 0;
            foreach (JsonElement x in e.EnumerateArray())
                r[k++] = x.GetSingle();
            return r;
        }

        static double[] Doubles(JsonElement e)
        {
            var r = new double[e.GetArrayLength()];
            int k = 0;
            foreach (JsonElement x in e.EnumerateArray())
                r[k++] = x.GetDouble();
            return r;
        }

        static int IntOr(JsonElement o, string name, int or) { return Has(o, name) ? Get(o, name).GetInt32() : or; }

        static double DoubleOr(JsonElement o, string name, double or) { return Has(o, name) ? Get(o, name).GetDouble() : or; }

        /// <summary>The host's Stage 09 "lights" object read as the player reads it (WmvIpcClient.LightsFrom: the float rows as floats).</summary>
        static LiquidLights LightsFromJson(JsonElement m)
        {
            var l = new LiquidLights
            {
                Product = Has(m, "product") ? Get(m, "product").GetString() : "", Build = Has(m, "build") ? Get(m, "build").GetString() : "",
                Map = IntOr(m, "map", 0), Time = IntOr(m, "time", 0), Slot = IntOr(m, "slot", 0), DefaultLight = IntOr(m, "defaultLight", 0),
                DefaultFrom = Has(m, "defaultFrom") ? Get(m, "defaultFrom").GetString() : "",
            };
            if (Has(m, "zones"))
                foreach (JsonElement z in Get(m, "zones").EnumerateArray())
                    l.AddZone(IntOr(z, "zone", 0), IntOr(z, "light", 0), DoubleOr(z, "zmin", 0), DoubleOr(z, "zmax", 0), IntOr(z, "conditional", 0),
                              Has(z, "poly") ? Doubles(Get(z, "poly")) : null);
            if (Has(m, "points"))
                foreach (JsonElement p in Get(m, "points").EnumerateArray())
                    l.AddPoint(IntOr(p, "light", 0), DoubleOr(p, "x", 0), DoubleOr(p, "y", 0), DoubleOr(p, "z", 0), DoubleOr(p, "start", 0), DoubleOr(p, "end", 0));
            if (Has(m, "values"))
                foreach (JsonElement v in Get(m, "values").EnumerateArray())
                    l.AddValues(IntOr(v, "light", 0), IntOr(v, "lightParams", 0), Floats(Get(v, "riverClose")), Floats(Get(v, "riverFar")),
                                Floats(Get(v, "oceanClose")), Floats(Get(v, "oceanFar")), Floats(Get(v, "waterAlpha")), Floats(Get(v, "oceanAlpha")),
                                Has(v, "sky") ? Floats(Get(v, "sky")) : null);
            return l;
        }

        /// <summary>The positions the invariant is checked at: every zone's centroid and vertices (at the middle of its Z band,
        /// clamped to +-1000), every point light's position and the points at its falloff start, between start and end and at
        /// its end (along +X), and a point no zone holds (the default light).</summary>
        static List<double[]> Positions(LiquidLights l)
        {
            var at = new List<double[]>();
            foreach (LiquidLightZone z in l.Zones)
            {
                double zm = Math.Max(-1000.0, Math.Min(1000.0, 0.5 * (Math.Max(z.ZMin, -1e6) + Math.Min(z.ZMax, 1e6))));
                double cx = 0, cy = 0;
                for (int k = 0; k < z.PointCount; k++)
                {
                    at.Add(new[] { z.Poly[2 * k], z.Poly[2 * k + 1], zm });
                    cx += z.Poly[2 * k];
                    cy += z.Poly[2 * k + 1];
                }
                at.Add(new[] { cx / z.PointCount, cy / z.PointCount, zm });
            }
            foreach (LiquidLightPoint p in l.Points)
            {
                at.Add(new[] { p.X, p.Y, p.Z });
                at.Add(new[] { p.X + p.Start, p.Y, p.Z });
                at.Add(new[] { p.X + 0.5 * (p.Start + p.End), p.Y, p.Z });
                at.Add(new[] { p.X + p.End, p.Y, p.Z });
            }
            at.Add(new[] { 1e7, 1e7, 0.0 });
            return at;
        }

        static void InvariantTests(string folder)
        {
            string[] files = Directory.GetFiles(folder, "lights_*_map*.json", SearchOption.TopDirectoryOnly);
            Array.Sort(files, StringComparer.Ordinal);
            int pairs = 0;
            foreach (string lightsPath in files)
            {
                string worldPath = Path.Combine(folder, Path.GetFileName(lightsPath).Replace("lights_", "worldlight_"));
                if (!File.Exists(worldPath))
                    continue;
                string name = Path.GetFileName(lightsPath).Substring("lights_".Length).Replace(".json", "");
                LiquidLights l;
                WorldLightSet s;
                using (JsonDocument ld = JsonDocument.Parse(File.ReadAllBytes(lightsPath)))
                using (JsonDocument wd = JsonDocument.Parse(File.ReadAllBytes(worldPath)))
                {
                    if (!Has(ld.RootElement, "values") || Get(wd.RootElement, "state").GetString() != "ok")
                    {
                        Check(false, "12:00 invariant, " + name + ": both objects are \"ok\" with values");
                        continue;
                    }
                    l = LightsFromJson(ld.RootElement);
                    s = WorldLightTests.FromJson(wd.RootElement);
                }
                pairs++;
                var w = new LiquidWaterDay();
                w.Update(l, s, LiquidWaterDay.Noon, false);
                // The table at 12:00 against Stage 09's rows, entry for entry.
                int same = 0, differ = 0;
                string firstDiff = null;
                foreach (LiquidLightValues row in l.Rows)
                {
                    LiquidLightValues v;
                    if (w.Table.TryGetValue(row.Light, out v) && SameValues(v, row))
                        same++;
                    else
                    {
                        differ++;
                        firstDiff = firstDiff ?? "Light " + row.Light;
                    }
                }
                bool table = l.Time == LiquidWaterDay.Noon && l.Slot == 0 && same == l.ValueCount && differ == 0 && w.Table.Count == l.ValueCount &&
                             l.DroppedValues == 0 && w.ZeroAtTime == 0 && w.NoonFallbacks == 0 && w.FromDay == l.ValueCount && w.AbsentColumns == null;
                // The selection at every position, Stage 09's against the table's.
                List<double[]> at = Positions(l);
                var a = new LiquidLightSample();
                var b = new LiquidLightSample();
                int equal = 0, valid = 0, withPoints = 0;
                string firstMiss = null;
                foreach (double[] q in at)
                {
                    l.Evaluate(q[0], q[1], q[2], a);
                    w.Evaluate(q[0], q[1], q[2], b);
                    if (SameSample(a, b))
                        equal++;
                    else
                        firstMiss = firstMiss ?? string.Format(System.Globalization.CultureInfo.InvariantCulture, "({0}, {1}, {2})", q[0], q[1], q[2]);
                    if (a.Valid) valid++;
                    if (a.Points > 0) withPoints++;
                }
                Check(table && equal == at.Count,
                      string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                    "12:00 invariant, {0}: the table at 1440 equals Stage 09's {1} value rows entry for entry, all 19 doubles bit-identical " +
                                    "({2} from the light's day, {3} kept at noon); the selection on it equals Stage 09's at {4} positions ({5} valid, {6} with " +
                                    "point lights; {7} zones, {8} point lights) -- validity, light, zone, params, points, values{9}",
                                    name, l.ValueCount, w.FromDay, w.NoonFallbacks, at.Count, valid, withPoints, l.Zones.Count, l.Points.Count,
                                    firstDiff != null ? "; first table difference " + firstDiff : firstMiss != null ? "; first difference at " + firstMiss : ""));
                // The time sweep: every 30 minutes the validity at every position is noon's, and every entry's colours are its day's.
                int sweepMiss = 0, entries = 0, colourMiss = 0, zeroSpans = 0;
                string firstSweep = null;
                var noonValid = new bool[at.Count];
                for (int k = 0; k < at.Count; k++)
                {
                    l.Evaluate(at[k][0], at[k][1], at[k][2], a);
                    noonValid[k] = a.Valid;
                }
                var rgb = new int[3];
                for (int t = 0; t < WorldLightDay.Day; t += 60)
                {
                    w.Update(l, s, t, false);
                    zeroSpans += w.ZeroAtTime;
                    foreach (KeyValuePair<int, LiquidLightValues> e in w.Table)
                    {
                        bool fb;
                        WorldLightParams p = s.ParamsOf(e.Key, 0, out fb);
                        if (p == null || ReferenceEquals(e.Value, null))
                            continue;
                        LiquidLightValues row;
                        l.TryGetValues(e.Key, out row);
                        if (ReferenceEquals(e.Value, row))
                            continue;                  // a row kept at noon
                        entries++;
                        s.DayAt(p.Day).Colour(t, WorldLightColumns.RiverFarColor, false, out rgb[0], out rgb[1], out rgb[2]);
                        if (e.Value.V[LiquidLightSample.RiverFar] != rgb[0] / 255.0 || e.Value.V[LiquidLightSample.RiverFar + 2] != rgb[2] / 255.0)
                            colourMiss++;
                    }
                    for (int k = 0; k < at.Count; k++)
                    {
                        w.Evaluate(at[k][0], at[k][1], at[k][2], b);
                        if (b.Valid != noonValid[k])
                        {
                            sweepMiss++;
                            firstSweep = firstSweep ?? "t " + t + " at (" + at[k][0] + ", " + at[k][1] + ")";
                        }
                    }
                }
                Check(sweepMiss == 0 && colourMiss == 0 && zeroSpans == 0 && entries > 0,
                      "12:00 invariant, " + name + ": every 30 minutes over the day the water's validity at each of the " + at.Count + " positions is " +
                      "noon's (no light loses its water colours on maps 0 and 1: the census) and " + entries + " entry-times take their day's colours" +
                      (firstSweep != null ? "; first change " + firstSweep : ""));
            }
            Check(pairs == 10, "12:00 invariant: " + pairs + " client-map pairs (five clients x maps 0 and 1) read from " + folder);
        }

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();
            log.Add("Water on the light's clock: the table");
            TableTests();
            log.Add("Water on the light's clock: the ease");
            EaseTests();
            log.Add("Water: the sky dome in a reflected direction");
            SkyWedgeTests();
            log.Add("Water: the fogged composition's twin");
            FogTwinTests();
            log.Add("Water: the switches");
            SwitchTests();
            string dumps = Environment.GetEnvironmentVariable("WMV_WORLD_LIGHT_HOST_DUMPS");
            log.Add("Water: the 12:00 invariant on the host's own dumps (maps 0 and 1 of five clients)");
            if (string.IsNullOrEmpty(dumps))
                log.Add("  SKIPPED: WMV_WORLD_LIGHT_HOST_DUMPS is not set");
            else
                InvariantTests(dumps);
            log.Add(string.Format("LiquidWaterDayTests: {0} failure(s)", failures));
            if (output != null)
                foreach (string line in log)
                    output(line);
            return failures;
        }
    }
}
