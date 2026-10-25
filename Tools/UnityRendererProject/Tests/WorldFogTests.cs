// WorldFogTests.cs
//
// Unit tests of Stage 10 step 7 (docs/world-viewer/10-light-sky-fog.md 4.0, 4.6, 6.9, 6.13): the client's per-pixel fog and the
// table-to-record mapping (Assets/Scripts/Wow/World/WorldFog.cs), the light sample's derived fog channels (WorldLight.cs,
// WorldLightState.cs), the sky's shared tint inputs (SkyDome.SunTint), the graded table (WorldLightModel.Fog) and the synthetic
// world model's material flags (WmoSynthetic.ContextRoot). The step-7 runner cases R1-R15 (after in-game test T7, R3, R5, R6,
// R7 and R8 check the corrected end rule E = min(0.933 x far clip, horizonStart), start FogScaler x min(E, 3000), and R15 is
// the rule itself):
//
//   R1   the client's terrain program as the target: WorldFog.Apply (kind 0) on the lit output of the Retail terrain program
//        2977247 p_1, re-executed offline with its own fog skip, reproduces the program's fog-on output within 2e-6, on 59
//        one-record cases covering every regime (embedded below; nothing in R1 comes from a hand model of the fog)
//   R2   the M2 program combiners_opaque 2977063 p_1 the same way: blend word 0 against kind 0, word 4 against kind 4 (rgb and
//        alpha), 16 cases; word 4's lit output carries the program's own premultiply (p_1:309-310) and kind 4 adds none
//   R3   Pack on the client's own keys (LightParams 5985 at 06:49:56, 11:53:30 and 18:46:13; 5951, 5944 and 6033 at 12:00), a
//        Retail Z key and one synthetic key per fall-back predicate, every row against an independent Python port of the
//        spec's mapping (fogpack_ref.py; R0.x and R0.y T7's rule instead), plus the values the spec's table names; and blends of a key with and a key without
//        each predicate at f = 0, 1e-6, 0.5, 1 - 1e-6 and 1: every row but the sky's tint inputs is the blend of the two keys'
//        rows (the per-key order: no jump at either end)
//   R4   SkyDome.Inputs after the SunTint extraction equals HEAD's Inputs (a copy below) on every key of 5985, 5944 and 5951 at
//        every half-minute, exactly; Pack's R5 and R7.x are the sky's angle, tint and weight
//   R5   ModeFor on every product, -wmvFog=off, the arms; AbsentColumns (the columns a mode's record needs, absent in the
//        layout or not sent); the public arm on Classic Era and MoP Classic LightParams 212 at
//        11:53 (start 2,382 yd, density 1 under Map Flags[2] 0x2 on Era, the derived 1.5 on MoP), the power36 arm (end 889,
//        start 706, rate 4.99, visibility 0.27 at 748 yd); each of the public arm's seven rules switched off alone changes
//        the record on a key that exercises it, and with all seven off the arm is the Retail record (no unlisted rule acts)
//        but for R0.x and R0.y (T7's rule is the Retail pattern's; the arm keeps min(F, 3000) x FogScaler and F)
//   R6   twin invariants: before the start visibility 1.0 and Apply returns the input itself; visibility non-increasing along
//        a ray; 0 beyond the end; kinds 1-4; the eye (raw 0) and R5.x = 1 give the untinted colour; Sat(NaN) = 0
//   R7   the switches -wmvFog and -wmvFogClassic, refused values as notes, DrawDefaults, the DescribeObjects texts (with
//        -wmvInteriorLight=outdoor and for an object lacking fog columns too)
//   R8   the graded table: every fog row graded with a state, every fall-back with its order, every arm rule its own row, no
//        row naming a map by number or listing values
//   R9   (with WMV_FOG_KEYS naming the key export of D:\wmv-10\step7\impl\fixtures\make_fog_keys.py, its manifest pinned) the
//        census counts on every key of the five clients against the census's printed lines; (with WMV_WORLD_LIGHT_FIXTURES)
//        the continuity of every record row but the sky's tint inputs across every half-minute of every day of maps 0 and 1
//        of the five clients and across an ease step, the largest jumps reported; and (always, on the embedded days) the
//        record against the public order for the tint inputs at 5985, 5944, 5951 and 6033
//   R10  with WMV_FOG_PARITY_OUT naming a file: Pack and the twin on a grid for the rig's parity check (fog_parity.py)
//   R11  WorldFog.Census on a hand-built set: every category at least once
//   R12  Pack allocates nothing over 10,000 calls and takes at most 5 microseconds (median)
//   R13  the light sample's existing channels equal HEAD's evaluator (a copy below) on every key of 5985, 5944, 5951 and 6033
//        at every half-minute, with and without point lights, and the derived block equals DeriveKey per key, then the blend
//   R14  WmoSynthetic.ContextRoot's old overload gives HEAD's bytes (SHA-256 pinned) and the new one with HEAD's two
//        materials the same bytes; other flags and blends are read back by the parser
//   R15  T7's end rule: Pack at far clips 1000 and 7000 and horizonStart 1000, 1900, 2500 and 3500 (E, the start, the cap at
//        3500, the end fade's zero at E); the colour ramp's and the polynomial path's far clip (ASSUMED, as committed) and the
//        blend's z E + (1 - z) F; the arms and the products the fog is off for untouched by horizonStart; -wmvGameHorizonStart
//        parsed, refused values as notes, the texts naming F, Hs and E; WMV_LIGHT=off untouched
// The fixtures at the end of this file are generated (their generator and inputs are named there with their SHA-256); the
// runner reads no file for them. Framework-free like the other suites: RunAll returns the failure count.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class WorldFogTests
    {
        static int failures;
        static readonly List<string> log = new List<string>();

        static void Check(bool condition, string name)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name); failures++; }
        }

        static void Info(string line) { log.Add("  INFO  " + line); }

        static string F(double v, string fmt = "0.######") { return v.ToString(fmt, CultureInfo.InvariantCulture); }

        static string E(double v) { return v.ToString("0.###e+0", CultureInfo.InvariantCulture); }

        /// <summary>|a - b| within eps x max(1, |b|).</summary>
        static bool Near(double a, double b, double eps) { return Math.Abs(a - b) <= eps * Math.Max(1.0, Math.Abs(b)); }

        static readonly double[] UpUnity = { 0.0, 1.0, 0.0 };
        static readonly long[] NoFlags = { 0, 0, 0 };
        const double Far = 7000.0, CameraZ = 40.0;
        /// <summary>The game's horizonStart the records are packed with (the default, -wmvGameHorizonStart's): the fog end is
        /// E = min(0.933 x Far, Hs) = 1900 on the Retail pattern.</summary>
        const double Hs = WorldLightModel.DefaultHorizonStart;

        // ================================================================================== the fixtures' shapes

        /// <summary>A day of one LightParams as the tests embed it: times, colours (keys x 24, 0xRRGGBB) and floats (keys x 26)
        /// in WorldLightColumns' orders, the params' Glow and Flags.</summary>
        sealed class FogDay
        {
            public readonly int[] Times, C;
            public readonly double[] Fl;
            public readonly double Glow;
            public readonly long Flags;

            public FogDay(int[] times, int[] c, double[] f, double glow, long flags)
            {
                Times = times;
                C = c;
                Fl = f;
                Glow = glow;
                Flags = flags;
            }

            public int Keys { get { return Times.Length; } }
        }

        sealed class PackCase
        {
            public readonly string Name;
            public readonly FogDay Day;
            public readonly int HalfMinute;
            public readonly double[] Sun, R;
            public readonly double FogHeight, Start, End, K;
            public readonly bool Artistic, Refused;

            public PackCase(string name, FogDay day, int halfMinute, double[] sun, double[] r, double fogHeight, double start, double end, double k,
                            bool artistic, bool refused)
            {
                Name = name;
                Day = day;
                HalfMinute = halfMinute;
                Sun = sun;
                R = r;
                FogHeight = fogHeight;
                Start = start;
                End = end;
                K = k;
                Artistic = artistic;
                Refused = refused;
            }
        }

        // ================================================================================== sets, samples, records

        static readonly double[] Everywhere = { -100000, -100000, 100000, -100000, 100000, 100000, -100000, 100000 };

        /// <summary>A light object whose one zone covers everything with Light 1 (LightParams 1001: `baseDay`); with `pointDay`
        /// also the point Light 2 (LightParams 1002) at the origin, weight 1 within 10 yd, 0 from 110 yd.</summary>
        static WorldLightSet SetOf(string product, long[] mapFlags, FogDay baseDay, FogDay pointDay = null)
        {
            var s = new WorldLightSet { State = "ok", Product = product, Map = 1, Build = "test", Branch = "test" };
            for (int k = 0; k < 3; k++)
                s.MapFlags[k] = mapFlags != null ? mapFlags[k] : 0;
            s.SetColumns(WorldLightColumns.Colours, WorldLightColumns.Floats, WorldLightColumns.Ints, new string[0]);
            int d0 = s.AddDay(baseDay.Times, baseDay.C, baseDay.Fl, new long[baseDay.Keys * WorldLightColumns.IntCount]);
            s.AddParams(1001, d0, baseDay.Glow, 0, baseDay.Flags, 0, new double[3], 0, 0);
            s.AddLight(1, new[] { 1001, 0, 0, 0, 0, 0, 0, 0 });
            s.AddZone(1, 1, 0, -64000, 64000, 0, Everywhere);
            if (pointDay != null)
            {
                int d1 = s.AddDay(pointDay.Times, pointDay.C, pointDay.Fl, new long[pointDay.Keys * WorldLightColumns.IntCount]);
                s.AddParams(1002, d1, pointDay.Glow, 0, pointDay.Flags, 0, new double[3], 0, 0);
                s.AddLight(2, new[] { 1002, 0, 0, 0, 0, 0, 0, 0 });
                s.AddPoint(2, 0, 0, 0, 10, 110);
            }
            return s;
        }

        static readonly WorldLightEvaluator evaluator = new WorldLightEvaluator();

        /// <summary>The sample at half-minute `hm`, at game point (x, 0, 0) (the point light's weight by x).</summary>
        static WorldLightSample SampleAt(WorldLightSet s, int hm, double x = 0.0)
        {
            var smp = new WorldLightSample();
            evaluator.Evaluate(s, x, 0, 0, hm * 30.0 + 15.0, 0, default(WorldLightRules), smp);
            return smp;
        }

        static WorldFogRecord PackOf(double[] v, int hm, double[] sun, WorldFogMode mode = WorldFogMode.RetailPattern, long[] flags = null,
                                     double far = Far, int rules = WorldFog.PublicAll, double hs = Hs)
        {
            var r = new WorldFogRecord();
            WorldFog.Pack(v, far, hs, CameraZ, hm, sun ?? new[] { 0.0, 1.0, 0.0 }, flags ?? NoFlags, mode, rules, r);
            return r;
        }

        static readonly double[] NoonSun = { -0.06162, 0.99619, 0.06162 };

        static double[] Floats32(string hexes, int at, int count)
        {
            string[] t = hexes.Split(' ');
            var a = new double[count];
            for (int k = 0; k < count; k++)
                a[k] = BitConverter.Int32BitsToSingle(int.Parse(t[at + k], NumberStyles.HexNumber, CultureInfo.InvariantCulture));
            return a;
        }

        static WorldFogRecord RecordOf(double[] values)
        {
            var r = new WorldFogRecord { Mode = WorldFogMode.RetailPattern };
            Array.Copy(values, r.R, 4 * WorldFogRecord.Rows);
            return r;
        }

        // ================================================================================== R1, R2: the client's programs

        static void ProgramTests()
        {
            double worst = 0.0;
            string worstCase = "";
            var regimes = new HashSet<string>();
            var o = new double[4];
            foreach (string row in TerrainProgramCases)
            {
                string[] parts = row.Split('|');
                string[] name = parts[0].Split(' ');
                foreach (string tag in name[1].Split('+'))
                    regimes.Add(tag);
                double[] v = Floats32(parts[1], 0, 70);
                WorldFogRecord r = RecordOf(v);
                double[] up = { v[56], v[57], v[58] }, p = { v[59], v[60], v[61] }, lit = { v[62], v[63], v[64], v[65] };
                WorldFog.Apply(r, p, up, lit, WorldFog.KindColour, o);
                for (int k = 0; k < 4; k++)
                {
                    double e = Math.Abs(o[k] - v[66 + k]);
                    if (e > worst)
                    {
                        worst = e;
                        worstCase = parts[0];
                    }
                }
            }
            string[] wanted = { "before-start", "exponential", "end-fade", "beyond-end", "hf", "poly0.00", "poly0.35", "poly0.80", "power-before",
                                "power-inside", "power-beyond", "tint", "pull", "w" };
            var missing = new List<string>();
            foreach (string w in wanted)
                if (!regimes.Contains(w))
                    missing.Add(w);
            Check(TerrainProgramCases.Length >= 40 && worst <= 2e-6 && missing.Count == 0,
                  "R1 the client's terrain program (Retail 2977247 p_1, re-executed offline): Apply (kind 0) on its fog-skip output gives its " +
                  "fog-on output on " + TerrainProgramCases.Length + " one-record cases, largest difference " + E(worst) + " (" + worstCase +
                  "; gate 2e-6); regimes covered: " + string.Join(", ", wanted) + (missing.Count > 0 ? "; MISSING " + string.Join(", ", missing) : ""));

            double worstRgb = 0.0, worstA = 0.0;
            int word0 = 0, word4 = 0;
            bool alphaBelowOne = false;
            foreach (string row in M2ProgramCases)
            {
                string[] parts = row.Split('|');
                int kind = int.Parse(parts[1], CultureInfo.InvariantCulture);
                double[] v = Floats32(parts[2], 0, 70);
                WorldFogRecord r = RecordOf(v);
                double[] up = { v[56], v[57], v[58] }, p = { v[59], v[60], v[61] }, lit = { v[62], v[63], v[64], v[65] };
                WorldFog.Apply(r, p, up, lit, kind, o);
                for (int k = 0; k < 3; k++)
                    worstRgb = Math.Max(worstRgb, Math.Abs(o[k] - v[66 + k]));
                worstA = Math.Max(worstA, Math.Abs(o[3] - v[69]));
                if (kind == WorldFog.KindFade)
                {
                    word4++;
                    alphaBelowOne |= v[65] < 0.999 && v[69] < v[65] - 1e-3;
                }
                else
                    word0++;
            }
            Check(word0 >= 16 && word4 >= 16 && worstRgb <= 2e-6 && worstA <= 2e-6 && alphaBelowOne,
                  "R2 the client's M2 program (combiners_opaque 2977063 p_1, one-record path, strength 1, interior weight 0, volume off): blend " +
                  "word 0 = kind 0 (" + word0 + " rows) and word 4 = kind 4, rgba x vis (" + word4 + " rows, alpha below 1 and faded), largest " +
                  "difference rgb " + E(worstRgb) + ", alpha " + E(worstA) + " (gate 2e-6)");
        }

        // ================================================================================== R3: Pack on the client's keys

        static string Bytes(double[] r, int at)
        {
            return string.Format(CultureInfo.InvariantCulture, "({0}, {1}, {2})", F(r[at] * 255, "0.##"), F(r[at + 1] * 255, "0.##"), F(r[at + 2] * 255, "0.##"));
        }

        static bool IsBytes(double[] r, int at, int red, int green, int blue)
        {
            return r[at] == red / 255.0 && r[at + 1] == green / 255.0 && r[at + 2] == blue / 255.0;
        }

        /// <summary>T7's end rule as the tests state it, independently of WorldFog's constants: E = min(0.933 x far clip, horizonStart).</summary>
        static double T7End(double far, double hs) { return Math.Min(0.933 * far, hs); }

        static void PackTests()
        {
            // The port (fogpack_ref.py) was written for the rule committed before in-game test T7: R0 = (min(F, 3000) x FogScaler,
            // F, ...). Every row but R0.x and R0.y is still the port's; those two are T7's rule, stated here from the case's own
            // inputs: R0.x = FogScaler x min(E, 3000) with FogScaler the port's (its R0.x / 3000, checked against the sample's), R0.y
            // = E on a record without the polynomial path and z E + (1 - z) F with it (z the port's R12.z).
            int matched = 0, polynomial = 0;
            string firstDiff = null;
            double e7 = T7End(Far, Hs);
            foreach (PackCase c in PackCases)
            {
                WorldLightSample smp = SampleAt(SetOf("wow", NoFlags, c.Day), c.HalfMinute);
                WorldFogRecord r = PackOf(smp.V, c.HalfMinute, c.Sun);
                double scaler = smp.Float(WorldLightColumns.FogScaler), zc = c.R[50];
                double wantStart = scaler * Math.Min(e7, 3000.0), wantEnd = zc >= 1.0 ? e7 : zc * e7 + (1.0 - zc) * Far;
                if (zc < 1.0)
                    polynomial++;
                bool port = Near(c.R[0], 3000.0 * scaler, 1e-12) && c.R[1] == Far && c.Start == c.R[0] && c.End == c.R[1];
                bool same = smp.Valid && port && Near(r.FogHeight, c.FogHeight, 1e-12) && Near(r.Start, wantStart, 1e-12) && Near(r.End, wantEnd, 1e-12) &&
                            Near(r.K, c.K, 1e-12) && r.Artistic == c.Artistic && r.ArtisticRefusedBlend == c.Refused && r.Mode == WorldFogMode.RetailPattern &&
                            r.EndRule == e7 && r.FarClip == Far && r.HorizonStart == Hs;
                for (int k = 0; k < 56; k++)
                {
                    double want = k == 0 ? wantStart : k == 1 ? wantEnd : c.R[k];
                    if (!Near(r.R[k], want, 1e-12))
                    {
                        same = false;
                        firstDiff = firstDiff ?? c.Name + " row " + k / 4 + "." + "xyzw"[k % 4] + ": " + F(r.R[k], "R") + " against " + F(want, "R");
                    }
                }
                if (same)
                    matched++;
                else
                    firstDiff = firstDiff ?? c.Name + ": the log fields" + (port ? "" : " (the port's R0 is not the pre-T7 rule)");
            }
            Check(matched == PackCases.Length && polynomial > 0,
                  "R3 Pack on the client's keys and the synthetic fall-back keys: every row of " + matched + " / " + PackCases.Length + " records equals " +
                  "the independent Python port's (fogpack_ref.py) within 1e-12, R0.x and R0.y T7's rule (start FogScaler x min(E, 3000), end E = " +
                  "min(0.933 x " + F(Far) + ", " + F(Hs) + ") = " + F(e7) + "; the far clip on the " + polynomial + " record(s) with the polynomial path)" +
                  (firstDiff != null ? "; first difference " + firstDiff : ""));

            // The values the spec's table names (5985; the state dump of the references' times).
            WorldLightSet sf = SetOf("wow", NoFlags, Day5985);
            WorldFogRecord dawn = PackOf(SampleAt(sf, 819).V, 819, NoonSun), noon = PackOf(SampleAt(sf, 1427).V, 1427, NoonSun),
                           dusk = PackOf(SampleAt(sf, 2252).V, 2252, NoonSun);
            WorldLightSample sNoon = SampleAt(sf, 1427);
            // T7's rule at the default far clip 7000 and horizonStart 1900: E = min(0.933 x 7000, 1900) = 1900, the start
            // FogScaler 0.15 x 1900 = 285 (the cap 3000 does not bind), the density as before.
            bool r0 = noon.R[0] == sNoon.Float(WorldLightColumns.FogScaler) * 1900.0 && Math.Abs(noon.R[0] - 285.0) < 1e-3 && noon.R[1] == 1900.0 &&
                      noon.EndRule == 1900.0 && noon.FarClip == 7000.0 && noon.HorizonStart == 1900.0 && dawn.R[1] == 1900.0 && dusk.R[1] == 1900.0 &&
                      noon.R[2] == sNoon.Float(WorldLightColumns.FogDensity) * 0.0005 && noon.R[3] == 0.0 &&
                      Math.Abs(dawn.R[2] - 0.001284) < 5e-7 && Math.Abs(noon.R[2] - 0.001495) < 5e-7 && Math.Abs(dusk.R[2] - 0.001049) < 5e-7;
            bool plane = noon.R[11] == CameraZ - 30.0 && Near(noon.R[15], 0.0025, 1e-7) && Near(noon.R[16], 0.0005, 1e-9) && noon.R[8] == 0.0 &&
                         noon.R[9] == 1.0 && noon.R[10] == 0.0 && noon.FogHeight == 30.0;
            bool cubic = Near(dawn.R[36], 0.18041, 5e-6) && Near(dawn.R[37], 0.39225, 5e-6) && Near(dawn.R[38], 0.43267, 5e-6) && Near(dawn.R[39], 0.00078, 5e-6) &&
                         Near(noon.R[36], 0.00387, 5e-6) && Near(noon.R[37], 0.00749, 5e-6) && Near(noon.R[38], 0.99177, 5e-6) && Near(noon.R[39], 0.00290, 5e-6) &&
                         Near(dusk.R[36], 0.00009, 5e-6) && Near(dusk.R[37], -0.00073, 5e-6) && Near(dusk.R[38], 1.00372, 5e-6) && Near(dusk.R[39], 0.00294, 5e-6);
            bool colours = IsBytes(dawn.R, 12, 91, 71, 71) && IsBytes(dawn.R, 17, 124, 89, 94) && IsBytes(dawn.R, 24, 103, 63, 56) && IsBytes(dawn.R, 52, 116, 83, 71) &&
                           IsBytes(noon.R, 12, 119, 75, 58) && IsBytes(noon.R, 17, 148, 91, 78) && IsBytes(noon.R, 24, 98, 47, 56) && IsBytes(noon.R, 52, 150, 86, 73) &&
                           IsBytes(dusk.R, 12, 125, 59, 62) && IsBytes(dusk.R, 17, 183, 80, 73) && IsBytes(dusk.R, 24, 98, 47, 56) && IsBytes(dusk.R, 52, 158, 83, 68);
            bool ramp = noon.R[55] == 2100.0 && noon.R[27] == 900.0 && Math.Abs(dusk.R[55] - 2190.22) < 0.01 && dusk.R[27] == 900.0;
            bool tint = dawn.R[20] == 0.0 && IsBytes(dawn.R, 21, 229, 166, 120) && Math.Abs(dawn.R[28] - 0.195) < 1e-4 && Near(noon.R[28], 0.3, 1e-7) &&
                        dusk.R[28] == 0.0 && noon.R[48] == 0.0 && noon.R[49] == 0.0 && noon.R[50] == 1.0 && noon.R[51] == 1.0 && !noon.Artistic;
            Check(r0 && plane && cubic && colours && ramp && tint,
                  "R3 the spec's 5985 values with T7's rule: R0 = (FogScaler x min(E, 3000) = " + F(noon.R[0], "0.####") + ", E = min(0.933 x 7000, " +
                  "1900) = " + F(noon.R[1]) + ", FogDensity x 0.0005 = " + F(dawn.R[2]) + " / " +
                  F(noon.R[2]) + " / " + F(dusk.R[2]) + ", 0); the plane camera Z - 30, rate 0.0025, kh 0.0005; R9 (program order) at the three times; " +
                  "SkyFog " + Bytes(dawn.R, 12) + " " + Bytes(noon.R, 12) + " " + Bytes(dusk.R, 12) + ", EndFog, FogHeight and EndFogHeight as the " +
                  "dump; the ramp from 2100 (2190.22 at dusk) over 900; the tint a 0, weight 0.195 / 0.3 / 0; no polynomial path");

            // The Z key: k 0, the end fade from 0.3 of the end only (T7: the end E = min(0.933 x F, horizonStart) = 1900).
            WorldFogRecord z = PackOf(SampleAt(SetOf("wow", NoFlags, DayZ), 1440).V, 1440, NoonSun);
            double zEnd = T7End(Far, Hs);
            double vNear, vMid, vFar, vBeyond, hf;
            WorldFog.Visibility(z, new[] { 0.29 * zEnd, 0.0, 0.0 }, UpUnity, out vNear, out hf);
            WorldFog.Visibility(z, new[] { 0.299 * zEnd, 0.0, 0.0 }, UpUnity, out vMid, out hf);
            WorldFog.Visibility(z, new[] { 0.65 * zEnd, 0.0, 0.0 }, UpUnity, out vFar, out hf);
            WorldFog.Visibility(z, new[] { zEnd + 1.0, 0.0, 0.0 }, UpUnity, out vBeyond, out hf);
            Check(z.R[2] == 0.0 && z.R[16] == 0.0 && z.R[1] == zEnd && vNear == 1.0 && vMid == 1.0 && Math.Abs(vFar - 0.5) < 1e-9 && vBeyond == 0.0,
                  "R3 a Retail Z key (FogDensity 0, FogEnd 0): k 0 and kh 0; the end E = " + F(zEnd) + "; visibility exactly 1 below 0.3 E, " + F(vFar) +
                  " at 0.65 E (the end fade), 0 beyond E (0.29 E and 0.299 E exactly 1)");

            // One synthetic key per fall-back predicate: what each rule puts in the record.
            WorldFogRecord[] x = new WorldFogRecord[Synthetic.Length];
            WorldLightSample[] xs = new WorldLightSample[Synthetic.Length];
            for (int k = 0; k < Synthetic.Length; k++)
            {
                xs[k] = SampleAt(SetOf("wow", NoFlags, Synthetic[k]), 1440);
                x[k] = PackOf(xs[k].V, 1440, NoonSun);
            }
            WorldFogRecord b = x[0];
            int sky = WorldLightSample.ColourAt(WorldLightColumns.SkyFogColor), endFog = WorldLightSample.ColourAt(WorldLightColumns.EndFogColor);
            bool endZero = x[1].R[17] == xs[1].V[sky] && x[1].R[18] == xs[1].V[sky + 1] && x[1].R[19] == xs[1].V[sky + 2] && x[1].R[52] == b.R[52];
            bool heightZero = x[2].R[24] == xs[2].V[sky] && x[2].R[25] == xs[2].V[sky + 1] && x[2].R[26] == xs[2].V[sky + 2];
            bool endHeightZero = x[3].R[52] == xs[3].V[endFog] && x[3].R[53] == xs[3].V[endFog + 1] && x[3].R[54] == xs[3].V[endFog + 2];
            bool bothZero = x[4].R[52] == xs[4].V[sky] && x[4].R[53] == xs[4].V[sky + 1] && x[4].R[54] == xs[4].V[sky + 2];
            bool rampZero = x[5].R[27] == Far && b.R[27] == 900.0;
            bool sunZero = x[6].R[20] == 1.0 && b.R[20] == 0.0;
            bool coefficientsZero = x[7].R[36] == 1.0 && x[7].R[37] == 0.0 && x[7].R[38] == 0.0 && x[7].R[39] == 0.0;
            bool scalerZero = x[8].R[16] == xs[8].Float(WorldLightColumns.FogDensity) * 0.0005 && x[8].R[15] == 0.0;
            bool polyOn = x[9].R[50] == 0.0 && x[9].Artistic && x[9].R[48] == xs[9].Float(WorldLightColumns.MainFogEndDist) &&
                          x[9].R[40] == xs[9].Float(WorldLightColumns.MainFogCoefficients + 3) && x[9].R[43] == xs[9].Float(WorldLightColumns.MainFogCoefficients) &&
                          x[9].R[44] == xs[9].Float(WorldLightColumns.HeightDensityFogCoeff + 3);
            bool polyRefused = x[10].R[50] == 1.0 && !x[10].Artistic && !x[10].ArtisticRefusedBlend;
            Check(endZero && heightZero && endHeightZero && bothZero && rampZero && sunZero && coefficientsZero && scalerZero && polyOn && polyRefused,
                  "R3 one key per fall-back: EndFogColor zero -> SkyFogColor (" + Bytes(x[1].R, 17) + "); FogHeightColor zero -> SkyFogColor; " +
                  "EndFogHeightColor zero -> the key's EndFogColor, and SkyFogColor when that is zero too; EndFogColorDistance 0 -> the far clip " +
                  "(not E: as committed, ASSUMED); " +
                  "SunFogColor zero -> threshold 1; coefficients zero -> u^3 (1, 0, 0, 0); FogHeightScaler 0 -> kh = FogDensity; the polynomial " +
                  "path on (z 0, the coefficients reversed) and refused (z 1)");

            // Blends of a key with and a key without each predicate: every row but the sky's tint inputs is the blend of the
            // two keys' rows (the derived channels are per key).
            double[] fs = { 0.0, 1e-6, 0.5, 1.0 - 1e-6, 1.0 };
            double worst = 0.0;
            string worstAt = "";
            var v = new double[WorldLightSample.Count];
            for (int k = 1; k < Synthetic.Length; k++)
                foreach (double f in fs)
                {
                    for (int j = 0; j < v.Length; j++)
                        v[j] = xs[0].V[j] + (xs[k].V[j] - xs[0].V[j]) * f;
                    WorldFogRecord r = PackOf(v, 1440, NoonSun);
                    for (int j = 0; j < 56; j++)
                    {
                        if ((j >= 20 && j < 24) || j == 28)
                            continue;
                        double want = b.R[j] + (x[k].R[j] - b.R[j]) * f;
                        double e = Math.Abs(r.R[j] - want) / Math.Max(1.0, Math.Abs(want));
                        if (e > worst)
                        {
                            worst = e;
                            worstAt = "key " + k + " f " + F(f, "R") + " row " + j / 4 + "." + "xyzw"[j % 4];
                        }
                    }
                }
            // A polynomial key blended with a refused one: the blended end falls below the blended start from f 0.714 on.
            var vh = new double[v.Length];
            for (int j = 0; j < v.Length; j++)
                vh[j] = xs[9].V[j] + (xs[10].V[j] - xs[9].V[j]) * 0.5;
            WorldFogRecord half = PackOf(vh, 1440, NoonSun);
            for (int j = 0; j < v.Length; j++)
                vh[j] = xs[9].V[j] + (xs[10].V[j] - xs[9].V[j]) * 0.8;
            WorldFogRecord late = PackOf(vh, 1440, NoonSun);
            Check(worst <= 1e-12 && half.R[50] == 0.5 && half.Artistic && !half.ArtisticRefusedBlend && late.R[50] == 1.0 && late.ArtisticRefusedBlend &&
                  !late.Artistic,
                  "R3 blends of the 5985 key with each fall-back key at f = 0, 1e-6, 0.5, 1 - 1e-6, 1: every row but R5 and R7.x (the sky's) is the " +
                  "blend of the two keys' rows (largest relative difference " + E(worst) + (worstAt.Length > 0 ? " at " + worstAt : "") + "; gate 1e-12): " +
                  "no jump at either end; a polynomial key with a refused one mixes the path by z 0.5 at f 0.5 and draws without it, counted, at f 0.8 " +
                  "(end below start)");
        }

        // ================================================================================== R4: the shared tint

        /// <summary>HEAD's SkyDome.Inputs (246ad137), before the SunTint extraction, verbatim in its arithmetic.</summary>
        static void HeadInputs(double[] v, int halfMinute, double farClip, bool endFog, SkyInputs s)
        {
            int[] slotColumns =
            {
                WorldLightColumns.SkyTopColor, WorldLightColumns.SkyMiddleColor, WorldLightColumns.SkyBand1Color, WorldLightColumns.SkyBand2Color,
                WorldLightColumns.SkySmogColor, WorldLightColumns.SkyFogColor,
            };
            for (int k = 0; k < SkyDome.ColourSlots; k++)
            {
                int at = WorldLightSample.ColourAt(slotColumns[k]);
                s.Colours[3 * k] = v[at];
                s.Colours[3 * k + 1] = v[at + 1];
                s.Colours[3 * k + 2] = v[at + 2];
            }
            int e = WorldLightSample.ColourAt(WorldLightColumns.EndFogColor);
            bool endFogZero = v[e] == 0.0 && v[e + 1] == 0.0 && v[e + 2] == 0.0;
            s.FogLean = 0.0;
            if (endFog && !endFogZero)
            {
                double w = SkyDome.FogLean(farClip, v[WorldLightSample.FloatAt(WorldLightColumns.EndFogColorDistance)]);
                s.FogLean = w;
                for (int c = 0; c < 3; c++)
                    s.Colours[15 + c] += (v[e + c] - s.Colours[15 + c]) * w;
            }
            int t = WorldLightSample.ColourAt(WorldLightColumns.SunFogColor);
            s.Tint[0] = v[t];
            s.Tint[1] = v[t + 1];
            s.Tint[2] = v[t + 2];
            bool tintZero = v[t] == 0.0 && v[t + 1] == 0.0 && v[t + 2] == 0.0;
            s.Angle = tintZero ? 1.0 : v[WorldLightSample.FloatAt(WorldLightColumns.SunFogAngle)];
            s.DayCurve = SkyDome.DayCurve(halfMinute);
            double w2 = SkyDome.SunAngleBlend * v[WorldLightSample.FloatAt(WorldLightColumns.SunFogStrength)] * s.DayCurve;
            s.WeightAbove = w2;
            s.WeightBelow = w2;
        }

        static bool SameInputs(SkyInputs a, SkyInputs b)
        {
            for (int k = 0; k < a.Colours.Length; k++)
                if (a.Colours[k] != b.Colours[k])
                    return false;
            for (int k = 0; k < 3; k++)
                if (a.Tint[k] != b.Tint[k])
                    return false;
            return a.Angle == b.Angle && a.WeightAbove == b.WeightAbove && a.WeightBelow == b.WeightBelow && a.DayCurve == b.DayCurve && a.FogLean == b.FogLean;
        }

        static void TintTests()
        {
            int compared = 0, differ = 0, packDiffer = 0, tinted = 0;
            var now = new SkyInputs();
            var head = new SkyInputs();
            var days = new[] { Day5985, Day5944, Day5951 };
            var names = new[] { "5985", "5944", "5951" };
            string first = null;
            for (int d = 0; d < days.Length; d++)
            {
                WorldLightSet s = SetOf("wow", NoFlags, days[d]);
                foreach (bool endFog in new[] { true, false })
                    for (int hm = 0; hm < WorldLightDay.Day; hm++)
                    {
                        WorldLightSample smp = SampleAt(s, hm);
                        SkyDome.Inputs(smp.V, hm, Far, endFog, now);
                        HeadInputs(smp.V, hm, Far, endFog, head);
                        compared++;
                        if (!SameInputs(now, head))
                        {
                            differ++;
                            first = first ?? names[d] + " at " + hm;
                        }
                        if (!endFog)
                            continue;
                        WorldFogRecord r = PackOf(smp.V, hm, NoonSun);
                        if (r.R[20] != head.Angle || r.R[21] != head.Tint[0] || r.R[22] != head.Tint[1] || r.R[23] != head.Tint[2] || r.R[28] != head.WeightAbove)
                            packDiffer++;
                        if (head.Angle < 1.0 && head.WeightAbove > 0.0)
                            tinted++;
                    }
            }
            Check(compared == 6 * WorldLightDay.Day && differ == 0 && packDiffer == 0 && tinted > 0,
                  "R4 the shared tint: SkyDome.Inputs after the SunTint extraction equals HEAD's Inputs exactly on every half-minute of 5985, 5944 and " +
                  "5951, with and without the ring-5 lean (" + compared + " states" + (first != null ? "; first difference " + first : "") + "); Pack's R5.x, " +
                  "R5.yzw and R7.x are the sky's angle, tint and weight on all of them (" + tinted + " with the tint on)");
        }

        // ================================================================================== R5: modes and arms

        static void ModeTests()
        {
            WorldFogMode wow = WorldFog.ModeFor("wow", true, WorldFogClassicArm.Off), wowt = WorldFog.ModeFor("wowt", true, WorldFogClassicArm.Off);
            string[] offProducts = { "wow_classic_era", "wow_classic", "wow_classic_beta", "", null, "wow_beta", "wowxptr" };
            bool off = true, armPublic = true, armPower = true, fogOff = true;
            foreach (string p in offProducts)
            {
                off &= WorldFog.ModeFor(p, true, WorldFogClassicArm.Off) == WorldFogMode.Off;
                bool named = !string.IsNullOrEmpty(p);
                armPublic &= WorldFog.ModeFor(p, true, WorldFogClassicArm.Public) == (named ? WorldFogMode.ClassicPublic : WorldFogMode.Off);
                armPower &= WorldFog.ModeFor(p, true, WorldFogClassicArm.Power36) == (named ? WorldFogMode.ClassicPower36 : WorldFogMode.Off);
            }
            foreach (string p in new[] { "wow", "wowt", "wow_classic_era", "wow_classic", "wow_classic_beta", "wow_beta" })
                foreach (WorldFogClassicArm arm in new[] { WorldFogClassicArm.Off, WorldFogClassicArm.Public, WorldFogClassicArm.Power36 })
                    fogOff &= WorldFog.ModeFor(p, false, arm) == WorldFogMode.Off;
            bool retailIgnoresArm = WorldFog.ModeFor("wow", true, WorldFogClassicArm.Public) == WorldFogMode.RetailPattern &&
                                    WorldFog.ModeFor("wowt", true, WorldFogClassicArm.Power36) == WorldFogMode.RetailPattern;
            Check(wow == WorldFogMode.RetailPattern && wowt == WorldFogMode.RetailPattern && off && armPublic && armPower && fogOff && retailIgnoresArm,
                  "R5 modes by client: wow and wowt the Retail pattern (an arm does not touch them); wow_classic_era, wow_classic, wow_classic_beta, " +
                  "wow_beta, wowxptr, \"\" and null off; -wmvFogClassic=public / power36 the arms on every named non-Retail product; -wmvFog=off off " +
                  "for all");

            // The columns a mode's record is made of, absent in the layout or not sent: named (the player then draws no fog).
            var full = new WorldLightSet { State = "ok", Product = "wow", Map = 1 };
            full.SetColumns(WorldLightColumns.Colours, WorldLightColumns.Floats, WorldLightColumns.Ints, new string[0]);
            var lacking = new WorldLightSet { State = "ok", Product = "wow", Map = 1 };
            lacking.SetColumns(WorldLightColumns.Colours, WorldLightColumns.Floats, WorldLightColumns.Ints, new[] { "FogScaler", "CloudDensity", "SkyFogColor" });
            var unsent = new List<string>(WorldLightColumns.Floats);
            unsent.Remove("FogEnd");
            unsent.Remove("HeightDensityFogCoeff3");
            var partial = new WorldLightSet { State = "ok", Product = "wow_classic_era", Map = 1 };
            partial.SetColumns(WorldLightColumns.Colours, unsent.ToArray(), WorldLightColumns.Ints, new string[0]);
            string lackingRetail = WorldFog.AbsentColumns(lacking, WorldFogMode.RetailPattern);
            string partialRetail = WorldFog.AbsentColumns(partial, WorldFogMode.RetailPattern);
            string partialArm = WorldFog.AbsentColumns(partial, WorldFogMode.ClassicPower36);
            bool absent = WorldFog.AbsentColumns(full, WorldFogMode.RetailPattern) == null &&
                          WorldFog.AbsentColumns(full, WorldFogMode.ClassicPublic) == null &&
                          WorldFog.AbsentColumns(full, WorldFogMode.ClassicPower36) == null && lackingRetail == "SkyFogColor, FogScaler" &&
                          WorldFog.AbsentColumns(lacking, WorldFogMode.Off) == null && WorldFog.AbsentColumns(null, WorldFogMode.RetailPattern) == null &&
                          partialRetail == "HeightDensityFogCoeff3" && partialArm == "FogEnd, HeightDensityFogCoeff3" &&
                          WorldFog.AbsentColumns(partial, WorldFogMode.ClassicPublic) == partialArm;
            Check(absent,
                  "R5 AbsentColumns: none on a set sending every column (each mode); absent in the layout -- \"" + lackingRetail + "\" (CloudDensity is " +
                  "not the fog's); not sent -- \"" + partialRetail + "\" for the Retail pattern, \"" + partialArm + "\" for the arms (FogEnd is theirs " +
                  "only); null for the mode Off and for no object");

            // The arms at the Southfury stand: LightParams 212 at 11:53 on Classic Era (Map Flags[2] 0x2) and MoP Classic (none).
            const int hm = 1426;
            WorldLightSample era = SampleAt(SetOf("wow_classic_era", MapFlagsEra, Day212Era), hm);
            WorldLightSample mop = SampleAt(SetOf("wow_classic", MapFlagsMop, Day212Mop), hm);
            WorldFogRecord ePub = PackOf(era.V, hm, NoonSun, WorldFogMode.ClassicPublic, MapFlagsEra);
            WorldFogRecord mPub = PackOf(mop.V, hm, NoonSun, WorldFogMode.ClassicPublic, MapFlagsMop);
            WorldFogRecord ePow = PackOf(era.V, hm, NoonSun, WorldFogMode.ClassicPower36, MapFlagsEra);
            double v748, v700, v890, hf;
            WorldFog.Visibility(ePow, new[] { 748.0, 0.0, 0.0 }, UpUnity, out v748, out hf);
            WorldFog.Visibility(ePow, new[] { 700.0, 0.0, 0.0 }, UpUnity, out v700, out hf);
            WorldFog.Visibility(ePow, new[] { 890.0, 0.0, 0.0 }, UpUnity, out v890, out hf);
            bool pub = (MapFlagsEra[2] & 0x2) != 0 && (MapFlagsMop[2] & 0x2) == 0 && Math.Abs(ePub.Start - 2382.0) < 1.0 && ePub.K == 0.0005 &&
                       Math.Abs(mPub.Start - 2382.0) < 1.0 && mPub.K == 1.5 * 0.0005 && ePub.End == Far;
            bool pow = ePow.Power && !ePow.PowerRefused && Math.Abs(ePow.End - 889.0) < 0.5 && Math.Abs(ePow.Start - 706.0) < 0.5 &&
                       Math.Abs(ePow.K - 4.99) < 0.005 && Math.Abs(v748 - 0.27) < 0.005 && v700 == 1.0 && v890 == 0.0 && ePow.R[4] == 1.0 &&
                       ePow.R[20] == 1.0 && ePow.R[28] == 0.0 && ePow.R[17] == ePow.R[12] && ePow.R[26] == ePow.R[14] && ePow.R[54] == ePow.R[14];
            Check(pub && pow,
                  "R5 the arms on LightParams 212 at 11:53: public -- Classic Era start " + F(ePub.Start, "0.#") + " yd, density 1 (Map Flags[2] 0x2), MoP " +
                  "Classic start " + F(mPub.Start, "0.#") + " yd, the derived density 1.5; power36 on Era -- end " + F(ePow.End, "0.#") + ", start " +
                  F(ePow.Start, "0.#") + ", rate " + F(ePow.K, "0.###") + ", visibility 1 at 700 yd, " + F(v748, "0.###") + " at 748, 0 beyond 889; " +
                  "SkyFogColor for all four colours, no tint");

            // Each rule of the public arm alone, on a state that exercises it; with all off the arm is the Retail record but for
            // T7's end rule, which is the Retail pattern's alone: the arm keeps R0 = (min(F, 3000) x FogScaler, F) as committed.
            WorldLightSample basis = SampleAt(SetOf("wow", NoFlags, Synthetic[0]), 1440);
            int fFogEnd = WorldLightSample.FloatAt(WorldLightColumns.FogEnd), fScaler = WorldLightSample.FloatAt(WorldLightColumns.FogScaler),
                fDensity = WorldLightSample.FloatAt(WorldLightColumns.FogDensity), fHeight = WorldLightSample.FloatAt(WorldLightColumns.FogHeight);
            long[] flag2 = { 0, 0, 2 };
            var states = new[]
            {
                new { Rule = WorldFog.PublicFlagDensity, FogEnd = 0.0, Scaler = 0.15, Density = 3.0, Height = 30.0, Flags = flag2, Far = Far },
                new { Rule = WorldFog.PublicDerivedDensity, FogEnd = 2000.0, Scaler = 0.5, Density = 0.0, Height = 30.0, Flags = NoFlags, Far = Far },
                new { Rule = WorldFog.PublicDensityFloor, FogEnd = 0.0, Scaler = 0.15, Density = 0.5, Height = 30.0, Flags = NoFlags, Far = Far },
                new { Rule = WorldFog.PublicScalerClamp, FogEnd = 0.0, Scaler = 1.5, Density = 3.0, Height = 30.0, Flags = NoFlags, Far = Far },
                new { Rule = WorldFog.PublicFogEndFloor, FogEnd = 0.0, Scaler = 0.0, Density = 0.0, Height = 30.0, Flags = NoFlags, Far = Far },
                new { Rule = WorldFog.PublicHeightClamp, FogEnd = 0.0, Scaler = 0.15, Density = 3.0, Height = 20000.0, Flags = NoFlags, Far = Far },
                new { Rule = WorldFog.PublicFarClipClamp, FogEnd = 0.0, Scaler = 0.15, Density = 3.0, Height = 30.0, Flags = NoFlags, Far = 500.0 },
            };
            var changed = new List<string>();
            bool each = true, none = true;
            foreach (var st in states)
            {
                var v = (double[])basis.V.Clone();
                v[fFogEnd] = st.FogEnd;
                v[fScaler] = st.Scaler;
                v[fDensity] = st.Density;
                v[fHeight] = st.Height;
                WorldFogRecord all = PackOf(v, 1440, NoonSun, WorldFogMode.ClassicPublic, st.Flags, st.Far, WorldFog.PublicAll);
                WorldFogRecord without = PackOf(v, 1440, NoonSun, WorldFogMode.ClassicPublic, st.Flags, st.Far, WorldFog.PublicAll & ~st.Rule);
                WorldFogRecord noRules = PackOf(v, 1440, NoonSun, WorldFogMode.ClassicPublic, st.Flags, st.Far, 0);
                WorldFogRecord retail = PackOf(v, 1440, NoonSun, WorldFogMode.RetailPattern, st.Flags, st.Far);
                bool differs = false;
                for (int k = 0; k < 56; k++)
                {
                    differs |= all.R[k] != without.R[k];
                    if (k >= 2)
                        none &= noRules.R[k] == retail.R[k];
                }
                double eRetail = T7End(st.Far, Hs);
                none &= noRules.R[0] == Math.Min(st.Far, 3000.0) * st.Scaler && noRules.R[1] == st.Far && noRules.EndRule == st.Far &&
                        retail.R[0] == st.Scaler * Math.Min(eRetail, 3000.0) && retail.R[1] == eRetail && retail.EndRule == eRetail;
                each &= differs;
                int bit = 0;
                while ((1 << bit) != st.Rule)
                    bit++;
                changed.Add(WorldFog.PublicRuleNames[bit] + (differs ? "" : " (NO CHANGE)"));
            }
            Check(each && none && states.Length == 7 && WorldFog.PublicRuleNames.Length == 7,
                  "R5 the public arm's seven rules, each switched off alone, change the record on a state that exercises it (" +
                  string.Join("; ", changed) + "); with all seven off the arm's record is the Retail pattern's (no unlisted rule acts) but for R0.x " +
                  "and R0.y: the arm's min(F, 3000) x FogScaler and F as committed, the Retail pattern's T7 rule FogScaler x min(E, 3000) and E = " +
                  "min(0.933 F, horizonStart)");
        }

        // ================================================================================== R6: twin invariants

        static void TwinTests()
        {
            WorldFogRecord r = PackOf(SampleAt(SetOf("wow", NoFlags, Day5985), 1440).V, 1440, NoonSun);
            double start = r.R[0], end = r.R[1];
            var lit = new[] { 0.31, 0.47, 0.22, 0.6 };
            var o = new double[4];
            bool near = start > 200.0;
            foreach (double d in new[] { 0.0, 1.0, 100.0, 0.5 * start, start - 0.1, start - 1e-6 })
                foreach (double h in new[] { -10.0, 0.0, 85.0 })
                {
                    double vis, hf;
                    double across = Math.Sqrt(Math.Max(0.0, d * d - h * h));
                    var p = new[] { across * 0.6, h, across * 0.8 };
                    WorldFog.Visibility(r, p, UpUnity, out vis, out hf);
                    WorldFog.Apply(r, p, UpUnity, lit, WorldFog.KindColour, o);
                    near &= vis == 1.0 && o[0] == lit[0] && o[1] == lit[1] && o[2] == lit[2] && o[3] == lit[3];
                }
            bool monotone = true, beyond = true;
            foreach (double h in new[] { -20.0, 0.0, 120.0 })
            {
                double last = 2.0;
                for (double d = 0.0; d <= 9000.0; d += 7.0)
                {
                    double vis, hf;
                    WorldFog.Visibility(r, new[] { d, h, 0.0 }, UpUnity, out vis, out hf);
                    monotone &= vis <= last;
                    last = vis;
                    if (d > end)
                        beyond &= vis == 0.0;
                }
            }
            double vMid, hfMid;
            var pMid = new[] { 900.0, 0.0, 300.0 };
            WorldFog.Visibility(r, pMid, UpUnity, out vMid, out hfMid);
            WorldFog.Apply(r, pMid, UpUnity, lit, WorldFog.KindBlack, o);
            bool k1 = o[0] == lit[0] * vMid + 0.0 * (1.0 - vMid) && o[1] == lit[1] * vMid && o[3] == lit[3] && vMid > 0.0 && vMid < 1.0;
            WorldFog.Apply(r, pMid, UpUnity, lit, WorldFog.KindFade, o);
            bool k4 = o[0] == lit[0] * vMid && o[2] == lit[2] * vMid && o[3] == lit[3] * vMid;
            var pFar = new[] { 8000.0, 0.0, 0.0 };
            WorldFog.Apply(r, pFar, UpUnity, lit, WorldFog.KindWhite, o);
            bool k2 = o[0] == 1.0 && o[1] == 1.0 && o[2] == 1.0 && o[3] == lit[3];
            WorldFog.Apply(r, pFar, UpUnity, lit, WorldFog.KindGrey, o);
            bool k3 = o[0] == 0.5 && o[1] == 0.5 && o[2] == 0.5 && o[3] == lit[3];
            WorldFog.Apply(r, pMid, UpUnity, lit, WorldFog.KindColour, o);
            var fc = new double[3];
            WorldFog.Colour(r, pMid, UpUnity, hfMid, fc);
            bool k0 = o[0] == lit[0] * vMid + fc[0] * (1.0 - vMid) && o[3] == lit[3];

            // The eye (raw 0) and a zero SunFogColor (R5.x = 1): finite and untinted.
            var tinted = new WorldFogRecord();
            r.CopyTo(tinted);
            tinted.R[20] = 0.0;
            tinted.R[28] = 0.4;
            for (int k = 0; k < 3; k++)
                tinted.R[32 + k] = UpUnity[k];
            var plain = new WorldFogRecord();
            tinted.CopyTo(plain);
            plain.R[28] = 0.0;
            var a = new double[3];
            var bb = new double[3];
            WorldFog.Colour(tinted, new[] { 0.0, 0.0, 0.0 }, UpUnity, 0.3, a);
            WorldFog.Colour(plain, new[] { 0.0, 0.0, 0.0 }, UpUnity, 0.3, bb);
            bool eye = !double.IsNaN(a[0]) && !double.IsInfinity(a[0]) && a[0] == bb[0] && a[1] == bb[1] && a[2] == bb[2];
            var noTint = new WorldFogRecord();
            tinted.CopyTo(noTint);
            noTint.R[20] = 1.0;
            var up = new[] { 0.0, 3000.0, 0.0 };
            WorldFog.Colour(noTint, up, UpUnity, 0.3, a);
            WorldFog.Colour(plain, up, UpUnity, 0.3, bb);
            var sanity = new double[3];
            WorldFog.Colour(tinted, up, UpUnity, 0.3, sanity);
            bool angleOne = !double.IsNaN(a[0]) && a[0] == bb[0] && a[1] == bb[1] && a[2] == bb[2] && sanity[0] != bb[0];
            bool sat = WorldFog.Sat(double.NaN) == 0.0 && WorldFog.Sat(double.PositiveInfinity) == 1.0 && WorldFog.Sat(double.NegativeInfinity) == 0.0 &&
                       WorldFog.Sat(-0.5) == 0.0 && WorldFog.Sat(0.25) == 0.25 && WorldFog.Sat(1.5) == 1.0;
            Check(near && monotone && beyond && k0 && k1 && k2 && k3 && k4 && eye && angleOne && sat,
                  "R6 the twin: before the start (" + F(start, "0.###") + " yd) visibility 1.0 and Apply returns the input itself; visibility " +
                  "non-increasing along rays at three heights and 0 beyond the end (" + F(end, "0.#") + " yd); kind 0 mixes towards the fog colour, 1 = rgb x vis, 4 = rgba x vis, " +
                  "2 / 3 white / grey at visibility 0; at the eye (raw 0) and with R5.x = 1 the colour is finite and untinted; Sat(NaN) = 0");
        }

        // ================================================================================== R7: switches

        static void SwitchTests()
        {
            WorldLightSwitches d = WorldLightSwitches.Parse(new string[0], null);
            WorldLightSwitches off = WorldLightSwitches.Parse(new[] { "-wmvFog=off" }, "on");
            WorldLightSwitches back = WorldLightSwitches.Parse(new[] { "-wmvFog=off", "-wmvFog=on" }, "on");
            WorldLightSwitches pub = WorldLightSwitches.Parse(new[] { "-wmvFogClassic=public" }, "on");
            WorldLightSwitches pow = WorldLightSwitches.Parse(new[] { "-wmvFogClassic=power36" }, "on");
            WorldLightSwitches none = WorldLightSwitches.Parse(new[] { "-wmvFogClassic=power36", "-wmvFogClassic=off" }, "on");
            WorldLightSwitches bad = WorldLightSwitches.Parse(new[] { "-wmvFog=maybe", "-wmvFogClassic=raw", "-wmvFogClassic=" }, null);
            Check(d.Fog && d.FogClassic == WorldFogClassicArm.Off && d.DrawDefaults && !off.Fog && !off.DrawDefaults && off.Notes.Count == 0 && back.Fog &&
                  back.DrawDefaults && pub.FogClassic == WorldFogClassicArm.Public && !pub.DrawDefaults && pub.Fog && pow.FogClassic == WorldFogClassicArm.Power36 &&
                  none.FogClassic == WorldFogClassicArm.Off && none.DrawDefaults && bad.Fog && bad.FogClassic == WorldFogClassicArm.Off && bad.Notes.Count == 3 &&
                  bad.Notes.TrueForAll(n => n.Contains("ignored") || n.Contains("refused")) && bad.DrawDefaults,
                  "R7 switches: the fog on and no arm by default (DrawDefaults); -wmvFog=off, -wmvFogClassic=public / power36 / off each taken, the " +
                  "last of two values wins, DrawDefaults false with an arm or the fog off; refused values are notes (" + string.Join(" | ", bad.Notes) + ")");
            string retail = d.DescribeObjects("wow"), ptr = d.DescribeObjects("wowt"), era = d.DescribeObjects("wow_classic_era"),
                   eraArm = pub.DescribeObjects("wow_classic_era"), mopArm = pow.DescribeObjects("wow_classic"), unknown = d.DescribeObjects(),
                   fogOff = off.DescribeObjects("wow"), beta = d.DescribeObjects("wow_beta");
            bool texts = retail.Contains("; fog: the client's per-pixel fog, layer 0, on the lit terrain, outdoor doodads and exterior groups (product wow, " +
                                         "Retail pattern; end E = min(0.933 x far clip, horizonStart) = min(0.933 x 7000, 1900) = 1900 yd, start FogScaler x " +
                                         "min(E, 3000), density FogDensity x 0.0005: MEASURED on the PTR 12.1.7 (post-data; the factor 0.933 at far clip 1000 " +
                                         "only; the end at horizonStart 1000 only, above it ASSUMED), Retail unverified; the cap 3000 INFERRED (public); the " +
                                         "colour ramp and the polynomial path read the far clip " +
                                         "(ASSUMED)); interiors, water, sky and particles unfogged") &&
                         ptr.Contains("(product wowt, Retail pattern") && era.Contains("; fog off for product wow_classic_era (D17/D18)") && !era.Contains("ARM") &&
                         eraArm.Contains("; fog off for product wow_classic_era (D17/D18) [ARM -wmvFogClassic=public: not compared with the game]") &&
                         mopArm.Contains("product wow_classic (D17/D18) [ARM -wmvFogClassic=power36: not compared with the game]") &&
                         unknown.Contains("product not known yet") && fogOff.Contains("; fog OFF (-wmvFog=off)") && !fogOff.Contains("per-pixel") &&
                         beta.Contains("fog off for product wow_beta") && retail.Contains("; exterior MOCV not drawn; water as Stage 09") &&
                         !retail.Contains("fog and water as Stage 09") && d.DescribeDraw().Contains("product not known yet") &&
                         !Regex.IsMatch(retail + era + eraArm + mopArm + unknown + fogOff, @"\b(warning|error|exception)\b", RegexOptions.IgnoreCase);
            Check(texts, "R7 DescribeObjects names the product and the mode: \"" + retail + "\" | \"" + eraArm + "\" | \"" + fogOff + "\"");

            // -wmvInteriorLight=outdoor: the copies interiors are bound to are fogged by their material flags alone, so the clause
            // names what it fogs; an object lacking fog columns is said to draw none.
            const string side = " fogged too (-wmvInteriorLight=outdoor, for the side-by-sides); ";
            string both = WorldLightSwitches.Parse(new[] { "-wmvInteriorLight=outdoor" }, "on").DescribeObjects("wow");
            string groupsOnly = WorldLightSwitches.Parse(new[] { "-wmvInteriorLight=outdoor", "-wmvDoodadLight=off" }, "on").DescribeObjects("wow");
            string doodadsOnly = WorldLightSwitches.Parse(new[] { "-wmvInteriorLight=outdoor", "-wmvWorldModelLight=off" }, "on").DescribeObjects("wow");
            string neither = WorldLightSwitches.Parse(new[] { "-wmvInteriorLight=outdoor", "-wmvWorldModelLight=off", "-wmvDoodadLight=off" }, "on")
                                               .DescribeObjects("wow");
            string eraInterior = WorldLightSwitches.Parse(new[] { "-wmvInteriorLight=outdoor", "-wmvFogClassic=public" }, "on")
                                                   .DescribeObjects("wow_classic_era");
            string lackingText = d.DescribeObjects("wow", "SkyFogColor, FogScaler"), unknownLacking = d.DescribeObjects(null, "SkyFogColor");
            bool interiors = both.Contains("; interior groups and the doodads they list" + side + "water, sky and particles unfogged") &&
                             !both.Contains("interiors, water") &&
                             groupsOnly.Contains("; interior groups" + side + "the doodads they list, water, sky and particles unfogged") &&
                             doodadsOnly.Contains("; the doodads interior groups list" + side + "interior groups, water, sky and particles unfogged") &&
                             neither.Contains("; interiors, water, sky and particles unfogged") &&
                             eraInterior.Contains("[ARM -wmvFogClassic=public: not compared with the game]; interior groups and the doodads they list" + side) &&
                             lackingText.Contains("; fog off for product wow (fog columns absent: SkyFogColor, FogScaler)") && !lackingText.Contains("per-pixel") &&
                             !lackingText.Contains("unfogged") && unknownLacking == d.DescribeObjects() && d.DescribeObjects("wow", null) == retail &&
                             !Regex.IsMatch(both + groupsOnly + doodadsOnly + lackingText, @"\b(warning|error|exception)\b", RegexOptions.IgnoreCase);
            Check(interiors, "R7 the fog clause under -wmvInteriorLight=outdoor names the interiors it fogs (\"" +
                             both.Substring(Math.Max(0, both.IndexOf("; fog:", StringComparison.Ordinal))) + "\"), and an object lacking fog columns " +
                             "draws none (\"" + lackingText.Substring(Math.Max(0, lackingText.IndexOf("; fog", StringComparison.Ordinal))) + "\")");

            // One formatter (log only): the state line's clause is WorldFogRecord.Summary, the text the player says and the rig's
            // foglog.py parses -- whole bytes, the arm in brackets.
            var sr = new WorldFogRecord { Mode = WorldFogMode.RetailPattern, Start = 285.0, K = 0.00105, End = 1900.0, FarClip = 7000.0, HorizonStart = 1900.0,
                                          EndRule = 1900.0 };
            sr.Colour[0] = 113.0 / 255.0; sr.Colour[1] = 67.0 / 255.0; sr.Colour[2] = 57.6 / 255.0;
            var sa = new WorldFogRecord();
            sr.CopyTo(sa);
            sa.Mode = WorldFogMode.ClassicPublic;
            var sp = new WorldFogRecord { Mode = WorldFogMode.ClassicPower36, Power = true, Start = 706.0, End = 889.0, K = 4.99 };
            var so = new WorldFogRecord();
            string tr = sr.Summary(), ta = sa.Summary(), tp = sp.Summary();
            var parser = new Regex(@"fog: start ([-\d.]+) yd, k ([-\d.eE]+) /yd, end ([-\d.]+) yd, colour \((\d+), (\d+), (\d+)\)");
            Match pm = parser.Match(tr);
            const string clause = "fog: start 285 yd, k 0.00105 /yd, end 1900 yd, colour (113, 67, 58)";
            bool one = tr == clause + ", E 1900 yd = min(0.933 x far clip 7000, horizonStart 1900)" && pm.Success && pm.Groups[6].Value == "58" &&
                       pm.Groups[3].Value == "1900" && sa.FarClip == 7000.0 && sa.HorizonStart == 1900.0 && sa.EndRule == 1900.0 &&
                       ta == clause + " [" + WorldFog.ModeName(WorldFogMode.ClassicPublic) + "]" &&
                       tp == "fog: power path, start 706 yd, end 889 yd, rate 4.99, colour (0, 0, 0) [" + WorldFog.ModeName(WorldFogMode.ClassicPower36) + "]" &&
                       so.Summary() == "fog: off";
            Check(one, "R7 one formatter: WorldFogRecord.Summary is the state line's clause in whole bytes that foglog.py parses, the Retail pattern's " +
                       "followed by its end rule's inputs (CopyTo carries them; the arms' text as committed) (\"" + tr + "\" | \"" + ta + "\" | \"" + tp + "\")");
        }

        // ================================================================================== R8: the graded table

        static void ModelTableTests()
        {
            // ASSUMED (step 7 after T7): the behaviour as committed, kept where the in-game evidence says nothing.
            string[] grades = { "PROVEN_BYTES", "PROVEN_SOURCE", "MEASURED", "INFERRED", "INFERRED (public)", "UNKNOWN", "ASSUMED" };
            string[] states = { WorldLightModel.StateApplied, WorldLightModel.StateHeld, WorldLightModel.StateNotApplied, WorldLightModel.StateArmOnly };
            var mapNumber = new Regex(@"\bmaps?\s*(ids?\s*)?[0-9]", RegexOptions.IgnoreCase);
            var list = new Regex(@"-?[0-9][0-9.]*\s*,\s*-?[0-9][0-9.]*\s*,\s*-?[0-9]");
            bool graded = true, stated = true, ordered = true, noMap = true, noList = true;
            int publicRows = 0, powerRows = 0, orders = 0;
            var names = new HashSet<string>();
            string bad = null;
            foreach (GradedValue v in WorldLightModel.Fog)
            {
                string text = v.Name + " " + v.Source;
                graded &= Array.IndexOf(grades, v.Grade) >= 0;
                stated &= Array.IndexOf(states, v.State) >= 0;
                bool fallBack = v.Name.Contains("fall-back") || v.Name.Contains(":=") || v.Name.StartsWith("polynomial path switch", StringComparison.Ordinal) ||
                                v.Name.StartsWith("sun tint", StringComparison.Ordinal);
                if (fallBack && v.Order != WorldLightModel.OrderPerKey && v.Order != WorldLightModel.OrderSky)
                {
                    ordered = false;
                    bad = bad ?? "no order: " + v.Name;
                }
                if (v.Order.Length > 0)
                    orders++;
                if (mapNumber.IsMatch(text))
                {
                    noMap = false;
                    bad = bad ?? "a map by number: " + text;
                }
                if (list.IsMatch(text) || double.IsNaN(v.Value) || double.IsInfinity(v.Value))
                {
                    noList = false;
                    bad = bad ?? "a list: " + text;
                }
                if (v.Name.StartsWith("public arm rule", StringComparison.Ordinal) && v.State == WorldLightModel.StateArmOnly)
                    publicRows++;
                if (v.Name.StartsWith("power36 arm", StringComparison.Ordinal) && v.State == WorldLightModel.StateArmOnly)
                    powerRows++;
                names.Add(v.Name);
            }
            int count = WorldLightModel.Table().Count;
            Check(graded && stated && ordered && noMap && noList && publicRows == 7 && powerRows == 5 && orders == 10 && names.Count == WorldLightModel.Fog.Length &&
                  count == 5 + 4 + 1 + WorldLightModel.Fog.Length + WorldLightModel.Rings.Length + WorldLightModel.Sky.Length + WorldLightModel.Terrain.Length +
                  WorldLightModel.Models.Length,
                  "R8 WorldLightModel.Fog: " + WorldLightModel.Fog.Length + " rows (the table " + count + "), each graded with a state; " + orders + " fall-backs " +
                  "with their order (per key, or the sky's); the public arm's 7 rules and the power36 arm's 5 each a row \"arm only\"; no row names a " +
                  "map by number or lists values" + (bad != null ? "; " + bad : ""));
            var byName = new Dictionary<string, GradedValue>();
            foreach (GradedValue v in WorldLightModel.Fog)
                byName[v.Name] = v;
            GradedValue cap = byName["fog start cap (yd): start = FogScaler x min(E, cap)"];
            GradedValue scale = byName["FogDensity scale per yard: k = FogDensity x scale"];
            GradedValue floor = byName["FogDensity floor (k = max(FogDensity, floor) x scale)"];
            GradedValue clamp = byName["FogScaler clamp (start = ... x clamp(FogScaler, -1, 1))"];
            GradedValue fade = byName["end fade slope sat(slope (1 - d / end))"];
            GradedValue b = byName["fog end E = min(b x far clip, horizonStart): the far clip's factor b"];
            GradedValue hsFactor = byName["fog end E: horizonStart's factor"];
            GradedValue startFactor = byName["fog start = FogScaler x factor x min(E, cap): the factor on E"];
            GradedValue hsDefault = byName["game horizonStart default (yd; -wmvGameHorizonStart)"];
            string[] assumedNames =
            {
                "fog end: b at far clips other than 1000 (1 = the same factor)", "fog end at horizonStart 1900 = horizonStart (1 = yes)",
                "fog end at horizonStart above 1900 (1 = the same rule)", "fog end at a horizonClip other than 7000 (1 = the same rule)",
                "colour ramp length on EndFogColorDistance <= 0: the far clip, not E (1 = as committed)",
                "end row on keys with the polynomial path: the far clip, not E (1 = as committed)", "Retail takes the pattern measured on the PTR (1 = yes)",
            };
            bool assumed = true;
            foreach (string n in assumedNames)
                assumed &= byName.ContainsKey(n) && byName[n].Grade == WorldLightModel.Assumed && byName[n].State == WorldLightModel.StateApplied &&
                           byName[n].Value == 1.0;
            int assumedRows = 0;
            foreach (GradedValue v in WorldLightModel.Fog)
                if (v.Grade == WorldLightModel.Assumed)
                    assumedRows++;
            bool t7 = b.Value == 0.933 && b.Value == WorldFog.EndFarClipFactor && b.Grade == "MEASURED" && b.Source.Contains("1000 ONLY") &&
                      hsFactor.Value == 1.0 && hsFactor.Grade == "MEASURED" && startFactor.Value == 1.0 && startFactor.Grade == "MEASURED" &&
                      scale.Grade == "MEASURED" && hsDefault.Value == 1900.0 && hsDefault.Value == WorldLightModel.DefaultHorizonStart &&
                      hsDefault.Grade == "PROVEN_BYTES" && hsDefault.Source.Contains("not a universal client default") &&
                      b.Source.Contains("Retail unverified") && assumed && assumedRows == assumedNames.Length &&
                      !byName.ContainsKey("fog end = the game far clip (1 = yes; FogEnd is not used on this pattern)");
            Check(cap.Value == WorldFog.StartCap && cap.Value == 3000.0 && cap.State == WorldLightModel.StateApplied && cap.Grade == "INFERRED (public)" &&
                  scale.Value == 0.0005 && scale.State == WorldLightModel.StateApplied && floor.Value == 0.9 && floor.State == WorldLightModel.StateNotApplied &&
                  clamp.State == WorldLightModel.StateNotApplied && fade.Value == 1.0 / 0.7 && fade.Grade == "PROVEN_BYTES" && t7 &&
                  cap.ToString().EndsWith("[applied]", StringComparison.Ordinal) &&
                  byName["colour fall-back: EndFogColor zero -> SkyFogColor (1 = applied)"].ToString().EndsWith("[applied, per key]", StringComparison.Ordinal),
                  "R8 the rows the packer reads, after T7: the end's far-clip factor 0.933 (MEASURED at far clip 1000 only), horizonStart's factor 1, the " +
                  "start's factor 1 on E and the density scale 0.0005 MEASURED (PTR, Retail unverified); the start cap 3000 applied, INFERRED (public); " +
                  "the horizonStart default 1900 PROVEN_BYTES (not a universal client default); " + assumedRows + " ASSUMED rows (b elsewhere, the end " +
                  "at 1900 and above, another horizonClip, the colour ramp and the polynomial path's far clip, Retail); the old \"end = the far clip\" " +
                  "row gone; step 3's density floor and FogScaler clamp kept, not applied (data as read); the end fade 1/0.7 PROVEN_BYTES; the state " +
                  "and order in the log text (" + cap + ")");
        }

        // ================================================================================== R9: census, continuity and order

        static string ShaOf(byte[] bytes) { return Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant(); }

        static string ShaOf(string path) { return ShaOf(File.ReadAllBytes(path)); }

        /// <summary>SHA-256 of the key export's manifest.json (D:\wmv-10\step7\impl\fixtures\keys, written 2026-10-23 by
        /// make_fog_keys.py from the survey's table extracts), which pins every export file.</summary>
        const string FogKeysManifestSha256 = "c2da58f7937399b3c41867a4aa4a6bad3708af6c2500e659019247c8c1a3b7ad";

        /// <summary>The census's counts per client (D:\wmv-10\step7\understand\out\census.txt, its "keys by pattern", "SunFogColor = 0",
        /// "FogDensity on E keys" and [rev2] lines): keys; E, L, Z; the u^3 fall-back and of it FogHeightDensity 0; kh := k; the ramp
        /// fall-back with EndFog set; polynomial and refused; SunFog set (keys minus "SunFogColor = 0"); E keys below 0.9.</summary>
        static readonly string[] CensusClients = { "wow", "wowt", "wow_classic_era", "wow_classic", "wow_classic_beta" };
        static readonly int[][] CensusCounts =
        {
            new[] { 19881, 19701, 0, 180, 6639, 307, 21, 356, 34, 2, 19881 - 16054, 40 },
            new[] { 19932, 19752, 0, 180, 6670, 307, 21, 359, 34, 2, 19932 - 16105, 40 },
            new[] { 1795, 114, 1666, 15, 2, 2, 0, 27, 0, 0, 1795 - 1770, 0 },
            new[] { 5690, 0, 5507, 183, 0, 0, 0, 0, 0, 0, 5690 - 5690, 0 },
            new[] { 5910, 216, 2430, 3264, 5, 2, 0, 27, 0, 0, 5910 - 5876, 4 },
        };

        static int[] CountsOf(WorldFogCensus c)
        {
            return new[] { c.Keys, c.E, c.L, c.Z, c.HeightFallBack, c.HeightFallBackNoDensity, c.KhFromK, c.RampFallBack, c.Polynomial, c.PolynomialRefused,
                           c.SunFogSet, c.DensityBelowFloor };
        }

        static void CensusCorpusTests(string folder)
        {
            string manifest = Path.Combine(folder, "manifest.json");
            string manifestSha = File.Exists(manifest) ? ShaOf(manifest) : "missing";
            Check(manifestSha == FogKeysManifestSha256, "R9 the key export: manifest.json has its pinned SHA-256 (" + manifestSha.Substring(0, Math.Min(16, manifestSha.Length)) + ")");
            if (manifestSha != FogKeysManifestSha256)
                return;
            var written = new Dictionary<string, string>();
            using (JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(manifest)))
                foreach (JsonProperty f in doc.RootElement.GetProperty("written").EnumerateObject())
                    written[f.Name] = f.Value.GetString();
            var colours = new int[WorldLightColumns.ColourCount];
            var floats = new double[WorldLightColumns.FloatCount];
            for (int c = 0; c < CensusClients.Length; c++)
            {
                string name = "fogkeys_" + CensusClients[c] + ".json";
                string path = Path.Combine(folder, name);
                string want;
                if (!written.TryGetValue(name, out want) || !File.Exists(path) || ShaOf(path) != want)
                {
                    Check(false, "R9 the key export: " + name + " is present with the manifest's SHA-256");
                    continue;
                }
                var census = new WorldFogCensus();
                double eShare, lShare, zShare;
                using (JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(path)))
                {
                    JsonElement root = doc.RootElement;
                    bool columns = true;
                    int k = 0;
                    foreach (JsonElement n in root.GetProperty("colourColumns").EnumerateArray())
                        columns &= n.GetString() == WorldLightColumns.Colours[k++];
                    k = 0;
                    foreach (JsonElement n in root.GetProperty("floatColumns").EnumerateArray())
                        columns &= n.GetString() == WorldLightColumns.Floats[k++];
                    Check(columns, "R9 the key export of " + CensusClients[c] + " lists the player's columns in the player's orders");
                    foreach (JsonElement row in root.GetProperty("keys").EnumerateArray())
                    {
                        int j = 0;
                        foreach (JsonElement x in row.EnumerateArray())
                        {
                            if (j >= 2 && j < 2 + colours.Length)
                                colours[j - 2] = x.GetInt32();
                            else if (j >= 2 + colours.Length)
                                floats[j - 2 - colours.Length] = x.GetDouble();
                            j++;
                        }
                        census.AddKey(colours, 0, floats, 0);
                    }
                }
                eShare = 100.0 * census.E / census.Keys;
                lShare = 100.0 * census.L / census.Keys;
                zShare = 100.0 * census.Z / census.Keys;
                int[] got = CountsOf(census);
                bool same = true;
                for (int k = 0; k < got.Length; k++)
                    same &= got[k] == CensusCounts[c][k];
                Check(same,
                      "R9 census of every " + CensusClients[c] + " key against the census's lines: " + census.Keys + " keys, E / L / Z " + F(eShare, "0.0") + " / " +
                      F(lShare, "0.0") + " / " + F(zShare, "0.0") + " %, u^3 fall-back " + census.HeightFallBack + " (FogHeightDensity 0: " +
                      census.HeightFallBackNoDensity + "), kh := k " + census.KhFromK + ", ramp fall-back with EndFog set " + census.RampFallBack +
                      ", polynomial " + census.Polynomial + " (" + census.PolynomialRefused + " refused), SunFog set " + census.SunFogSet + ", E below 0.9 " +
                      census.DensityBelowFloor + (same ? "" : "; want " + string.Join(", ", CensusCounts[c])));
            }
        }

        /// <summary>The light state of a day alone at half-minute t (the evaluator's sample without point lights).</summary>
        static void DaySample(WorldLightDay d, int t, double[] v, int[] rgb, double[] fl, long[] ints, double[] fog)
        {
            d.Evaluate(t, false, rgb, fl, ints, fog);
            Array.Clear(v, 0, v.Length);
            for (int k = 0; k < rgb.Length; k++)
                v[k] = rgb[k] / 255.0;
            for (int k = 0; k < fl.Length; k++)
                v[WorldLightSample.FloatBase + k] = fl[k];
            for (int k = 0; k < fog.Length; k++)
                v[WorldLightSample.FogBase + k] = WorldFog.SampleValue(k, fog[k]);
        }

        static bool ColourRow(int j) { return (j >= 12 && j < 15) || (j >= 17 && j < 20) || (j >= 24 && j < 27) || (j >= 52 && j < 55); }

        static bool TintRow(int j) { return (j >= 20 && j < 24) || j == 28; }

        sealed class Place
        {
            public double Value;
            public string Where;
        }

        static void Keep(List<Place> top, double value, string where)
        {
            if (top.Count == 5 && value <= top[4].Value)
                return;
            top.Add(new Place { Value = value, Where = where });
            top.Sort((a, b) => b.Value.CompareTo(a.Value));
            if (top.Count > 5)
                top.RemoveAt(5);
        }

        static string Top(List<Place> top, string fmt)
        {
            var s = new List<string>();
            foreach (Place p in top)
                s.Add(F(p.Value, fmt) + " (" + p.Where + ")");
            return string.Join(", ", s);
        }

        /// <summary>The LightParams that name each day of a set (for naming places).</summary>
        static Dictionary<int, int> ParamsOfDays(WorldLightSet s)
        {
            var map = new Dictionary<int, int>();
            var lights = new HashSet<int>();
            foreach (WorldLightZone z in s.Zones) lights.Add(z.Light);
            foreach (WorldLightPoint p in s.Points) lights.Add(p.Light);
            if (s.DefaultLight > 0) lights.Add(s.DefaultLight);
            foreach (int light in lights)
            {
                int[] slots;
                if (!s.TryGetSlots(light, out slots))
                    continue;
                foreach (int id in slots)
                {
                    WorldLightParams p;
                    if (id != 0 && s.TryGetParams(id, out p) && p.Day >= 0 && !map.ContainsKey(p.Day))
                        map[p.Day] = id;
                }
            }
            return map;
        }

        static readonly double[] ReportDistances = { 300.0, 600.0, 1000.0, 2000.0 };

        static void ContinuityTests(string folder)
        {
            string manifest = Path.Combine(folder, "manifest.json");
            string manifestSha = File.Exists(manifest) ? ShaOf(manifest) : "missing";
            Check(manifestSha == WorldLightTests.FixtureManifestSha256,
                  "R9 continuity: the offline fixtures' manifest.json has its pinned SHA-256 (" + manifestSha.Substring(0, Math.Min(16, manifestSha.Length)) + ")");
            if (manifestSha != WorldLightTests.FixtureManifestSha256)
                return;
            var v = new double[WorldLightSample.Count];
            var rgb = new int[3 * WorldLightColumns.ColourCount];
            var fl = new double[WorldLightColumns.FloatCount];
            var ints = new long[WorldLightColumns.IntCount];
            var fog = new double[WorldFog.DerivedCount];
            var r0 = new WorldFogRecord();
            var r1 = new WorldFogRecord();
            var fc0 = new double[3];
            var fc1 = new double[3];
            double[] sun = NoonSun;
            double easeFactor = LiquidLightEase.Factor(1.0 / 60.0);
            foreach (string client in CensusClients)
            {
                int steps = 0, violations = 0, easeSteps = 0, easeViolations = 0, days = 0;
                string first = null;
                var visTop = new List<Place>();
                var colourTop = new List<Place>();
                var tintTop = new List<Place>();
                var shares = new List<string>();
                foreach (int map in new[] { 0, 1 })
                {
                    string path = Path.Combine(folder, client + "_map" + map + ".json");
                    if (!File.Exists(path))
                        continue;
                    WorldLightSet set;
                    using (JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(path)))
                        set = WorldLightTests.FromJson(doc.RootElement);
                    WorldFogCensus census = WorldFog.Census(set);
                    shares.Add("map " + map + ": E / L / Z " + census.E + " / " + census.L + " / " + census.Z + " of " + census.Keys + " keys named");
                    Dictionary<int, int> named = ParamsOfDays(set);
                    for (int di = 0; di < set.DayCount; di++)
                    {
                        WorldLightDay d = set.DayAt(di);
                        if (d == null)
                            continue;
                        days++;
                        int lp;
                        string dayName = "map " + map + " LightParams " + (named.TryGetValue(di, out lp) ? lp.ToString(CultureInfo.InvariantCulture) : "day " + di);
                        int n = d.Keys;
                        var keyRec = new double[n][];
                        for (int k = 0; k < n; k++)
                        {
                            DaySample(d, d.Times[k], v, rgb, fl, ints, fog);
                            WorldFog.Pack(v, Far, Hs, CameraZ, d.Times[k], sun, NoFlags, WorldFogMode.RetailPattern, r0);
                            keyRec[k] = (double[])r0.R.Clone();
                        }
                        DaySample(d, 0, v, rgb, fl, ints, fog);
                        WorldFog.Pack(v, Far, Hs, CameraZ, 0, sun, NoFlags, WorldFogMode.RetailPattern, r0);
                        for (int t = 0; t < WorldLightDay.Day; t++)
                        {
                            int t1 = (t + 1) % WorldLightDay.Day;
                            DaySample(d, t1, v, rgb, fl, ints, fog);
                            WorldFog.Pack(v, Far, Hs, CameraZ, t1, sun, NoFlags, WorldFogMode.RetailPattern, r1);
                            int a = -1, b = -1;
                            for (int k = 0; k < n; k++)
                                if (d.Times[k] <= t)
                                    a = k;
                            for (int k = 0; k < n && b < 0; k++)
                                if (d.Times[k] >= t + 1)
                                    b = k;
                            double ta = a >= 0 ? d.Times[a] : d.Times[n - 1] - WorldLightDay.Day;
                            double tb = b >= 0 ? d.Times[b] : d.Times[0] + WorldLightDay.Day;
                            if (a < 0) a = n - 1;
                            if (b < 0) b = 0;
                            double share = 1.0 / (tb - ta);
                            steps++;
                            for (int j = 0; j < 56; j++)
                            {
                                if (TintRow(j))
                                    continue;
                                double bound = Math.Abs(keyRec[b][j] - keyRec[a][j]) * share + (ColourRow(j) ? 1.0 / 255.0 : 0.0) +
                                               1e-12 * Math.Max(1.0, Math.Max(Math.Abs(r0.R[j]), Math.Abs(r1.R[j])));
                                if (Math.Abs(r1.R[j] - r0.R[j]) > bound)
                                {
                                    violations++;
                                    first = first ?? dayName + " at half-minute " + t + ", row " + j / 4 + "." + "xyzw"[j % 4] + ": " + E(Math.Abs(r1.R[j] - r0.R[j])) +
                                                     " over " + E(bound);
                                }
                            }
                            // Reported: visibility and colour at the plane's height and 100 yd above it; the tint inputs.
                            double plane0 = r0.FogHeight - CameraZ, plane1 = r1.FogHeight - CameraZ;
                            foreach (double dist in ReportDistances)
                                foreach (double above in new[] { 0.0, 100.0 })
                                {
                                    double vis0, hf0, vis1, hf1;
                                    var p0 = new[] { dist * 0.8, plane0 + above, dist * 0.6 };
                                    var p1 = new[] { dist * 0.8, plane1 + above, dist * 0.6 };
                                    WorldFog.Visibility(r0, p0, UpUnity, out vis0, out hf0);
                                    WorldFog.Visibility(r1, p1, UpUnity, out vis1, out hf1);
                                    WorldFog.Colour(r0, p0, UpUnity, hf0, fc0);
                                    WorldFog.Colour(r1, p1, UpUnity, hf1, fc1);
                                    string where = dayName + " " + WorldClock.Format(t * 30.0) + " " + F(dist, "0") + " yd" + (above > 0 ? " +100" : "");
                                    Keep(visTop, Math.Abs(vis1 - vis0), where);
                                    Keep(colourTop, 255.0 * Math.Max(Math.Abs(fc1[0] - fc0[0]), Math.Max(Math.Abs(fc1[1] - fc0[1]), Math.Abs(fc1[2] - fc0[2]))), where);
                                }
                            Keep(tintTop, Math.Max(Math.Abs(r1.R[20] - r0.R[20]), Math.Abs(r1.R[28] - r0.R[28])),
                                 dayName + " " + WorldClock.Format(t * 30.0) + " (a " + F(r0.R[20], "0.###") + " -> " + F(r1.R[20], "0.###") + ")");
                            r1.CopyTo(r0);
                        }
                        // One ease step at 60 frames a second, from the state at t towards the state 2 h later.
                        var target = new double[v.Length];
                        var eased = new double[v.Length];
                        var rt = new WorldFogRecord();
                        var re = new WorldFogRecord();
                        for (int t = 0; t < WorldLightDay.Day; t += 97)
                        {
                            DaySample(d, (t + 240) % WorldLightDay.Day, target, rgb, fl, ints, fog);
                            DaySample(d, t, v, rgb, fl, ints, fog);
                            for (int k = 0; k < v.Length; k++)
                                eased[k] = v[k] + (target[k] - v[k]) * easeFactor;
                            WorldFog.Pack(v, Far, Hs, CameraZ, t, sun, NoFlags, WorldFogMode.RetailPattern, r0);
                            WorldFog.Pack(target, Far, Hs, CameraZ, t, sun, NoFlags, WorldFogMode.RetailPattern, rt);
                            WorldFog.Pack(eased, Far, Hs, CameraZ, t, sun, NoFlags, WorldFogMode.RetailPattern, re);
                            easeSteps++;
                            for (int j = 0; j < 56; j++)
                                if (!TintRow(j) &&
                                    Math.Abs(re.R[j] - r0.R[j]) > easeFactor * Math.Abs(rt.R[j] - r0.R[j]) + 1e-12 * Math.Max(1.0, Math.Abs(rt.R[j])))
                                {
                                    easeViolations++;
                                    first = first ?? dayName + " ease step at " + t + ", row " + j / 4 + "." + "xyzw"[j % 4];
                                }
                        }
                    }
                }
                if (days == 0)
                {
                    Info("R9 continuity: no fixture of " + client + " maps 0 and 1");
                    continue;
                }
                Check(violations == 0 && easeViolations == 0,
                      "R9 continuity, " + client + " maps 0 and 1 (" + days + " days, " + steps + " half-minute steps, " + easeSteps + " ease steps): every " +
                      "record row but R5 and R7.x (the sky's) changes by at most the bracketing keys' difference times the step's share, plus one " +
                      "byte step on the colour rows (the host's rounding) and 1e-12; an ease step moves each row by the ease factor of its distance at " +
                      "most" + (first != null ? "; first " + first : ""));
                Info("R9 " + client + ": " + string.Join("; ", shares));
                Info("R9 " + client + ": largest visibility change between half-minutes (300 / 600 / 1000 / 2000 yd, at the plane and 100 yd " +
                     "above): " + Top(visTop, "0.####"));
                Info("R9 " + client + ": largest fog-colour change (steps): " + Top(colourTop, "0.##"));
                Info("R9 " + client + ": largest jump of the sky's tint inputs (R5.x or R7.x): " + Top(tintTop, "0.####"));
            }
        }

        /// <summary>The record against the public per-key order for the tint inputs as well (SunFogColor zero -> no tint per key,
        /// the angle blended, the public angle blend), at 5985, 5944, 5951 and 6033 on every half-minute.</summary>
        static void OrderTests()
        {
            var names = new[] { "5985", "5944", "5951", "6033" };
            var days = new[] { Day5985, Day5944, Day5951, Day6033 };
            double worstVis = 0.0, worstColour = 0.0;
            int flips = 0, states = 0;
            var fc = new double[3];
            var fcAlt = new double[3];
            var alt = new WorldFogRecord();
            int ct = WorldLightColumns.SunFogColor, fa = WorldLightColumns.SunFogAngle, fs = WorldLightColumns.SunFogStrength;
            for (int di = 0; di < days.Length; di++)
            {
                WorldLightSet set = SetOf("wow", NoFlags, days[di]);
                WorldLightDay d = set.DayAt(0);
                for (int t = 0; t < WorldLightDay.Day; t++)
                {
                    WorldLightSample smp = SampleAt(set, t);
                    double gx, gy, gz;
                    WorldLightModel.SunDirection(t * 30.0 + 15.0, out gx, out gy, out gz);
                    var sun = new[] { -gy, gz, gx };
                    WorldFogRecord r = PackOf(smp.V, t, sun);
                    int a, b;
                    double f;
                    d.Bracket(t, false, out a, out b, out f);
                    double a1 = (d.Colours[a * 24 + ct] & 0xFFFFFF) == 0 ? 1.0 : d.Floats[a * 26 + fa];
                    double a2 = (d.Colours[b * 24 + ct] & 0xFFFFFF) == 0 ? 1.0 : d.Floats[b * 26 + fa];
                    double s1 = d.Floats[a * 26 + fs], s2 = d.Floats[b * 26 + fs];
                    double blend, angle, strength;
                    if (a1 >= 1.0 && a2 >= 1.0) { blend = 1.0; angle = a1 + (a2 - a1) * f; strength = 0.0; }
                    else if (a1 >= 1.0) { blend = f; angle = a2; strength = s2; }
                    else if (a2 >= 1.0) { blend = 1.0 - f; angle = a1; strength = s1; }
                    else { blend = 1.0; angle = a1 + (a2 - a1) * f; strength = s1 + (s2 - s1) * f; }
                    if ((a1 >= 1.0) != (a2 >= 1.0))
                        flips++;
                    r.CopyTo(alt);
                    alt.R[20] = angle;
                    alt.R[28] = SkyDome.SunAngleBlend * blend * strength * SkyDome.DayCurve(t);
                    states++;
                    foreach (double dist in new[] { 300.0, 1000.0, 3000.0 })
                        for (int az = 0; az < 360; az += 30)
                            foreach (double el in new[] { -5.0, 0.0, 5.0, 20.0 })
                            {
                                double ce = Math.Cos(el * Math.PI / 180.0), se = Math.Sin(el * Math.PI / 180.0);
                                var p = new[] { dist * ce * Math.Cos(az * Math.PI / 180.0), dist * se, dist * ce * Math.Sin(az * Math.PI / 180.0) };
                                double vis, hf, visAlt, hfAlt;
                                WorldFog.Visibility(r, p, UpUnity, out vis, out hf);
                                WorldFog.Visibility(alt, p, UpUnity, out visAlt, out hfAlt);
                                WorldFog.Colour(r, p, UpUnity, hf, fc);
                                WorldFog.Colour(alt, p, UpUnity, hfAlt, fcAlt);
                                worstVis = Math.Max(worstVis, Math.Abs(vis - visAlt));
                                for (int k = 0; k < 3; k++)
                                    worstColour = Math.Max(worstColour, 255.0 * Math.Abs(fc[k] - fcAlt[k]));
                            }
                }
            }
            Check(flips == 0 && worstVis <= 1e-12 && worstColour <= 0.5,
                  "R9 order: the record against the public per-key order for the tint inputs too, at " + string.Join(", ", names) + " on every " +
                  "half-minute (" + states + " states, " + flips + " predicate flips): largest visibility difference " + E(worstVis) + " (gate 1e-12), " +
                  "fog colour " + F(worstColour, "0.####") + " step (gate 0.5) towards every azimuth");
        }

        // ================================================================================== R10: the parity grid for the rig

        /// <summary>Pack and the twin on a grid, for the rig's fog_parity.py (its Python port's parity check): a header line, then per LightParams (5985,
        /// 5944, 5951, 6033) and half-hour (48) a line "R lp hm" + the 56 record values + FogHeight, and per fragment height h (game Z
        /// 20, 120, 300; the camera at game Z 40, the sun of the public path) and distance d (0 to 3000 yd by 50, along the Unity
        /// azimuth 0.7 rad) a line "P lp hm h d vis hf r g b". Tab-separated, doubles round-trip ("R").</summary>
        static void ParityGrid(string path)
        {
            var sb = new StringBuilder();
            sb.Append("# WorldFogTests R10: far clip 7000, horizonStart 1900 (T7's end rule: R0.y = min(0.933 x 7000, 1900) = 1900, R0.x = FogScaler x " +
                      "min(E, 3000)), camera game Z 40, up (0, 1, 0); R lines: lp hm R0.x..R13.w FogHeight; P lines: lp hm h d vis hf r g b\n");
            var lps = new[] { 5985, 5944, 5951, 6033 };
            var days = new[] { Day5985, Day5944, Day5951, Day6033 };
            var fc = new double[3];
            int lines = 0;
            for (int di = 0; di < days.Length; di++)
            {
                WorldLightSet set = SetOf("wow", NoFlags, days[di]);
                for (int hh = 0; hh < 48; hh++)
                {
                    int hm = 60 * hh;
                    double gx, gy, gz;
                    WorldLightModel.SunDirection(hm * 30.0 + 15.0, out gx, out gy, out gz);
                    WorldFogRecord r = PackOf(SampleAt(set, hm).V, hm, new[] { -gy, gz, gx });
                    sb.Append("R\t").Append(lps[di]).Append('\t').Append(hm);
                    for (int k = 0; k < 56; k++)
                        sb.Append('\t').Append(r.R[k].ToString("R", CultureInfo.InvariantCulture));
                    sb.Append('\t').Append(r.FogHeight.ToString("R", CultureInfo.InvariantCulture)).Append('\n');
                    lines++;
                    foreach (double h in new[] { 20.0, 120.0, 300.0 })
                        for (double d = 0.0; d <= 3000.0; d += 50.0)
                        {
                            var p = new[] { d * Math.Cos(0.7), h - CameraZ, d * Math.Sin(0.7) };
                            double vis, hf;
                            WorldFog.Visibility(r, p, UpUnity, out vis, out hf);
                            WorldFog.Colour(r, p, UpUnity, hf, fc);
                            sb.Append("P\t").Append(lps[di]).Append('\t').Append(hm).Append('\t').Append(h.ToString("R", CultureInfo.InvariantCulture)).Append('\t')
                              .Append(d.ToString("R", CultureInfo.InvariantCulture));
                            foreach (double x in new[] { vis, hf, fc[0], fc[1], fc[2] })
                                sb.Append('\t').Append(x.ToString("R", CultureInfo.InvariantCulture));
                            sb.Append('\n');
                            lines++;
                        }
                }
            }
            File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
            Info("R10 parity grid: " + lines + " lines written to " + path + " (the rig's fog_parity.py compares them with fogmodel.py)");
        }

        // ================================================================================== R11: the census on a synthetic set

        static void CensusTests()
        {
            int cc = WorldLightColumns.ColourCount, fc = WorldLightColumns.FloatCount;
            int keys = 13;
            var times = new int[keys];
            var c = new int[keys * cc];
            var f = new double[keys * fc];
            for (int k = 0; k < keys; k++)
            {
                times[k] = 100 * k;
                c[k * cc + WorldLightColumns.SkyFogColor] = 0x405060;
                f[k * fc + WorldLightColumns.FogDensity] = 3.0;
                f[k * fc + WorldLightColumns.EndFogColorDistance] = 0.0;
            }
            Action<int, int, double> set = (k, column, value) => f[k * fc + column] = value;
            set(0, WorldLightColumns.FogDensity, 2.0);            // E with height coefficients set and a rate: nothing else
            set(0, WorldLightColumns.FogScaler, 0.2);
            set(0, WorldLightColumns.FogHeightCoefficients + 1, 1.0);
            set(0, WorldLightColumns.FogHeightScaler, 0.01);
            set(0, WorldLightColumns.EndFogColorDistance, 500.0);
            set(1, WorldLightColumns.FogDensity, 0.0);            // L
            set(1, WorldLightColumns.FogEnd, 1000.0);
            set(1, WorldLightColumns.FogScaler, 0.5);
            set(2, WorldLightColumns.FogDensity, 0.0);            // Z
            set(3, WorldLightColumns.MainFogCoefficients + 1, 0.5);  // polynomial on
            set(3, WorldLightColumns.MainFogStartDist, 100.0);
            set(3, WorldLightColumns.MainFogEndDist, 900.0);
            set(4, WorldLightColumns.HeightDensityFogCoeff, 0.2);    // polynomial refused
            set(4, WorldLightColumns.MainFogStartDist, 900.0);
            set(4, WorldLightColumns.MainFogEndDist, 100.0);
            set(5, WorldLightColumns.FogHeightScaler, 0.02);         // the u^3 fall-back, FogHeightDensity 0
            set(6, WorldLightColumns.FogHeightScaler, 0.02);         // the u^3 fall-back
            set(6, WorldLightColumns.FogHeightDensity, 1.0);
            for (int i = 0; i < 4; i++)                              // kh := k acts: rate 0, coefficients summing to 0.7
                set(7, WorldLightColumns.FogHeightCoefficients + i, new[] { 0.1, 0.2, 0.3, 0.1 }[i]);
            set(8, WorldLightColumns.EndFogColorDistance, -5.0);     // the ramp fall-back with EndFog set
            c[8 * cc + WorldLightColumns.EndFogColor] = 0x506070;
            c[9 * cc + WorldLightColumns.SunFogColor] = 0x806040;    // SunFog set
            set(10, WorldLightColumns.FogDensity, 0.5);              // below 0.9
            set(11, WorldLightColumns.FogScaler, 1.5);               // FogScaler outside [0, 1]
            set(12, WorldLightColumns.FogScaler, -0.3);
            var s = new WorldLightSet { State = "ok", Product = "wow", Map = 9 };
            s.SetColumns(WorldLightColumns.Colours, WorldLightColumns.Floats, WorldLightColumns.Ints, new string[0]);
            int dayA = s.AddDay(times, c, f, new long[keys * 2]);
            var cb = new int[cc];
            var fb = new double[fc];
            fb[WorldLightColumns.FogDensity] = 1.0;
            fb[WorldLightColumns.FogScaler] = 0.1;
            int dayB = s.AddDay(new[] { 0 }, cb, fb, new long[2]);
            s.AddParams(1001, dayA, 0, 0, 0x4, 0, new double[3], 0, 0);
            s.AddParams(1002, dayB, 0, 0, 0, 0, new double[3], 0, 0);
            s.AddParams(1003, -1, 0, 0, 0x4, 0, new double[3], 0, 0);
            s.AddLight(1, new[] { 1001, 0, 0, 0, 0, 0, 0, 0 });      // the zone's
            s.AddLight(2, new[] { 1002, 1001, 0, 0, 0, 0, 0, 0 });   // the default light (slot 1 not counted)
            s.AddLight(3, new[] { 1001, 0, 0, 0, 0, 0, 0, 0 });      // a point light naming the zone's day again
            s.AddLight(4, new[] { 1003, 0, 0, 0, 0, 0, 0, 0 });      // a point light without keys
            s.AddZone(1, 1, 0, -64000, 64000, 0, Everywhere);
            s.DefaultLight = 2;
            s.AddPoint(3, 0, 0, 0, 10, 20);
            s.AddPoint(4, 50, 0, 0, 10, 20);
            WorldFogCensus n = WorldFog.Census(s);
            int[] got = { n.Days, n.Keys, n.E, n.L, n.Z, n.Polynomial, n.PolynomialRefused, n.HeightFallBack, n.HeightFallBackNoDensity, n.KhFromK, n.RampFallBack,
                          n.SunFogSet, n.Flag4Params, n.DensityBelowFloor, n.ScalerOutside };
            int[] want = { 2, 14, 12, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 2 };
            bool same = true;
            for (int k = 0; k < want.Length; k++)
                same &= got[k] == want[k];
            string line = n.Line(s.Map, s.Product, WorldFogMode.RetailPattern);
            WorldFogCensus empty = WorldFog.Census(null);
            Check(same && empty.Keys == 0 && line.StartsWith("fog for map 9 (product wow): Retail pattern; keys: E 12, L 1, Z 1 (k 0: end fade only); polynomial path 2 (1 refused",
                                                             StringComparison.Ordinal) &&
                  !Regex.IsMatch(line, @"\b(warning|error|exception)\b", RegexOptions.IgnoreCase),
                  "R11 the census of a synthetic set (two days, a day named twice counted once, slot 1 and a light without keys left out): " +
                  string.Join(", ", got) + (same ? "" : " (want " + string.Join(", ", want) + ")") + "; the line: " + line);
        }

        // ================================================================================== R12: no allocation

        static void AllocationTests()
        {
            double[] v = SampleAt(SetOf("wow", NoFlags, Day5985), 1427).V;
            var r = new WorldFogRecord();
            var lit = new[] { 0.3, 0.4, 0.5, 1.0 };
            var o = new double[4];
            var p = new[] { 800.0, 20.0, 300.0 };
            WorldFog.Pack(v, Far, Hs, CameraZ, 1427, NoonSun, NoFlags, WorldFogMode.RetailPattern, r);
            WorldFog.Apply(r, p, UpUnity, lit, WorldFog.KindColour, o);
            long before = GC.GetAllocatedBytesForCurrentThread();
            for (int k = 0; k < 10000; k++)
                WorldFog.Pack(v, Far, Hs, CameraZ, 1427, NoonSun, NoFlags, WorldFogMode.RetailPattern, r);
            long packBytes = GC.GetAllocatedBytesForCurrentThread() - before;
            before = GC.GetAllocatedBytesForCurrentThread();
            for (int k = 0; k < 10000; k++)
                WorldFog.Apply(r, p, UpUnity, lit, WorldFog.KindColour, o);
            long applyBytes = GC.GetAllocatedBytesForCurrentThread() - before;
            var times = new double[21];
            for (int b = 0; b < times.Length; b++)
            {
                Stopwatch w = Stopwatch.StartNew();
                for (int k = 0; k < 500; k++)
                    WorldFog.Pack(v, Far, Hs, CameraZ, 1427, NoonSun, NoFlags, WorldFogMode.RetailPattern, r);
                times[b] = w.Elapsed.TotalMilliseconds * 1000.0 / 500.0;
            }
            Array.Sort(times);
            double median = times[times.Length / 2];
            Check(packBytes == 0 && applyBytes == 0 && median <= 5.0,
                  "R12 Pack allocates " + packBytes + " bytes over 10,000 calls and Apply " + applyBytes + "; Pack takes " + F(median, "0.###") +
                  " microseconds (median of 21 batches; budget 5)");
        }

        // ================================================================================== R13: the light sample's channels

        static void HeadBracket(int[] times, int t, bool clamp, out int a, out int b, out double f)
        {
            int n = times.Length;
            a = b = -1;
            for (int k = 0; k < n; k++)
                if (times[k] == t)
                    a = b = k;
            double ta = 0.0, tb = 0.0;
            if (a < 0)
            {
                if (clamp && t < times[0])
                    a = b = 0;
                else if (clamp && t > times[n - 1])
                    a = b = n - 1;
                else
                {
                    int before = -1, after = -1;
                    for (int k = 0; k < n; k++)
                    {
                        if (times[k] < t)
                            before = k;
                        else if (after < 0)
                            after = k;
                    }
                    a = before >= 0 ? before : n - 1;
                    ta = before >= 0 ? times[before] : times[n - 1] - WorldLightDay.Day;
                    b = after >= 0 ? after : 0;
                    tb = after >= 0 ? times[after] : times[0] + WorldLightDay.Day;
                }
            }
            f = (a == b || tb <= ta) ? 0.0 : (t - ta) / (tb - ta);
        }

        static int HeadChannel(int from, int to, int shift, double f)
        {
            double va = (from >> shift) & 0xFF;
            double vb = (to >> shift) & 0xFF;
            int v = (int)Math.Floor(va + (vb - va) * f + 0.5);
            return Math.Min(255, Math.Max(0, v));
        }

        static int HeadRound(double va, double vb, double f)
        {
            int v = (int)Math.Floor(va + (vb - va) * f + 0.5);
            return Math.Min(255, Math.Max(0, v));
        }

        /// <summary>HEAD's WorldLightDay.Evaluate (246ad137), on the day's arrays.</summary>
        static void HeadDayEvaluate(WorldLightDay d, int t, bool clamp, int[] rgb, double[] floats)
        {
            int a, b;
            double f;
            HeadBracket(d.Times, t, clamp, out a, out b, out f);
            int cc = WorldLightColumns.ColourCount, fc = WorldLightColumns.FloatCount;
            for (int j = 0; j < cc; j++)
            {
                int ca = d.Colours[a * cc + j], cb = d.Colours[b * cc + j];
                rgb[3 * j] = HeadChannel(ca, cb, 16, f);
                rgb[3 * j + 1] = HeadChannel(ca, cb, 8, f);
                rgb[3 * j + 2] = HeadChannel(ca, cb, 0, f);
            }
            for (int j = 0; j < fc; j++)
            {
                double x = d.Floats[a * fc + j], y = d.Floats[b * fc + j];
                floats[j] = x + (y - x) * f;
            }
        }

        /// <summary>HEAD's WorldLightEvaluator.Evaluate (246ad137), the channels 0 .. Glow.</summary>
        static void HeadSample(WorldLightSet set, double x, double seconds, double[] v, WorldLightSelection sel)
        {
            Array.Clear(v, 0, v.Length);
            set.Select(x, 0, 0, 0, default(WorldLightRules), sel);
            if (!sel.Valid)
                return;
            int t = WorldClock.HalfMinuteOf(seconds);
            var baseRgb = new int[3 * WorldLightColumns.ColourCount];
            var rgb = new int[3 * WorldLightColumns.ColourCount];
            var floats = new double[WorldLightColumns.FloatCount];
            HeadDayEvaluate(sel.Day, t, false, baseRgb, floats);
            for (int k = 0; k < baseRgb.Length; k++)
                v[k] = baseRgb[k] / 255.0;
            for (int k = 0; k < floats.Length; k++)
                v[WorldLightSample.FloatBase + k] = floats[k];
            v[WorldLightSample.Glow] = sel.ParamsRow.Glow;
            foreach (LightGeometry.Weighted<WorldLightParams> p in sel.Points)
            {
                HeadDayEvaluate(set.DayAt(p.Item.Day), t, false, rgb, floats);
                double w = p.W;
                for (int k = 0; k < rgb.Length; k++)
                    v[k] += (rgb[k] / 255.0 - v[k]) * w;
                for (int k = 0; k < floats.Length; k++)
                    v[WorldLightSample.FloatBase + k] += (floats[k] - v[WorldLightSample.FloatBase + k]) * w;
                v[WorldLightSample.Glow] += (p.Item.Glow - v[WorldLightSample.Glow]) * w;
            }
        }

        /// <summary>The derived block as DeriveKey on the two bracketing keys, then the day's blend, then the point lights' lerp.</summary>
        static void ReferenceDerived(WorldLightSet set, double x, double seconds, double[] o, WorldLightSelection sel)
        {
            set.Select(x, 0, 0, 0, default(WorldLightRules), sel);
            int t = WorldClock.HalfMinuteOf(seconds);
            DerivedOf(sel.Day, t, o);
            var p = new double[WorldFog.DerivedCount];
            foreach (LightGeometry.Weighted<WorldLightParams> pt in sel.Points)
            {
                DerivedOf(set.DayAt(pt.Item.Day), t, p);
                for (int k = 0; k < o.Length; k++)
                    o[k] += (p[k] - o[k]) * pt.W;
            }
        }

        static void DerivedOf(WorldLightDay d, int t, double[] o)
        {
            int a, b;
            double f;
            HeadBracket(d.Times, t, false, out a, out b, out f);
            var da = new double[WorldFog.DerivedCount];
            var db = new double[WorldFog.DerivedCount];
            WorldFog.DeriveKey(d.Colours, a * WorldLightColumns.ColourCount, d.Floats, a * WorldLightColumns.FloatCount, da, 0);
            WorldFog.DeriveKey(d.Colours, b * WorldLightColumns.ColourCount, d.Floats, b * WorldLightColumns.FloatCount, db, 0);
            for (int k = 0; k < o.Length; k++)
                o[k] = k < WorldFog.DerivedColours ? da[k] + (db[k] - da[k]) * f : HeadRound(da[k], db[k], f) / 255.0;
        }

        static void SampleTests()
        {
            var pairs = new[] { new[] { 0, 1 }, new[] { 1, 2 }, new[] { 2, 3 }, new[] { 3, 0 } };
            var days = new[] { Day5985, Day5944, Day5951, Day6033 };
            var names = new[] { "5985", "5944", "5951", "6033" };
            var head = new double[WorldLightSample.Glow + 1];
            var derived = new double[WorldFog.DerivedCount];
            var sel = new WorldLightSelection();
            int states = 0, channelDiffer = 0, derivedDiffer = 0, withPoints = 0;
            string first = null;
            foreach (int[] pair in pairs)
            {
                WorldLightSet set = SetOf("wow", NoFlags, days[pair[0]], days[pair[1]]);
                foreach (double x in new[] { 500.0, 5.0, 60.0, 85.0 })
                    for (int t = 0; t < WorldLightDay.Day; t++)
                    {
                        double seconds = t * 30.0 + 15.0;
                        WorldLightSample smp = SampleAt(set, t, x);
                        HeadSample(set, x, seconds, head, sel);
                        ReferenceDerived(set, x, seconds, derived, sel);
                        states++;
                        if (smp.Points > 0)
                            withPoints++;
                        for (int k = 0; k <= WorldLightSample.Glow; k++)
                            if (smp.V[k] != head[k])
                            {
                                channelDiffer++;
                                first = first ?? names[pair[0]] + " over " + names[pair[1]] + " at x " + x + ", " + t + ": channel " + k;
                                break;
                            }
                        for (int k = 0; k < derived.Length; k++)
                            if (smp.V[WorldLightSample.FogBase + k] != derived[k])
                            {
                                derivedDiffer++;
                                first = first ?? names[pair[0]] + " over " + names[pair[1]] + " at x " + x + ", " + t + ": derived " + k;
                                break;
                            }
                    }
            }
            Check(channelDiffer == 0 && derivedDiffer == 0 && withPoints == 3 * 4 * WorldLightDay.Day && WorldLightSample.FogBase == WorldLightSample.Glow + 1 &&
                  WorldLightSample.Count == WorldLightSample.FogBase + WorldFog.DerivedCount,
                  "R13 the light sample: channels 0 .. Glow equal HEAD's evaluator exactly, and the derived block DeriveKey per key then the blend, on " +
                  "every half-minute of 5985, 5944, 5951 and 6033, each under another as a point light at weight 0, 1, 0.5 and 0.25 (" + states +
                  " states" + (first != null ? "; first difference " + first : "") + "); the block after Glow (" + WorldFog.DerivedCount + " channels)");
        }

        // ================================================================================== R14: WmoSynthetic.ContextRoot

        /// <summary>SHA-256 of HEAD's (246ad137) ContextRoot bytes for the two calls below (computed with HEAD's sources).</summary>
        const string ContextRootSha256A = "2a06f5c964958cadbafbea8c4af5f1c1414c710c02957ca94ed0082fc4eddc40";
        const string ContextRootSha256B = "209a9c6882cbe2a81b5a0d539b7189a291c421575eabd0a7029afd438a7d9c58";

        static void WmoSyntheticTests()
        {
            var modd = new byte[2][];
            for (int r = 0; r < 2; r++)
            {
                modd[r] = new byte[40];
                for (int k = 0; k < 40; k++)
                    modd[r][k] = (byte)(7 * r + 3 * k + 1);
            }
            uint[] flagsA = { WmoGroupFlags.Outdoor, WmoGroupFlags.Indoor, WmoGroupFlags.Outdoor | WmoGroupFlags.Indoor, 0u };
            uint[] flagsB = { WmoGroupFlags.Outdoor, WmoGroupFlags.Indoor };
            byte[] a = WmoSynthetic.ContextRoot(flagsA, 7000, 7100, null);
            byte[] b = WmoSynthetic.ContextRoot(flagsB, 7200, 7300, modd);
            byte[] a2 = WmoSynthetic.ContextRoot(flagsA, 7000, 7100, null, new[] { 0u, WmoMaterial.FlagCullDisabled });
            byte[] b2 = WmoSynthetic.ContextRoot(flagsB, 7200, 7300, modd, new[] { 0u, WmoMaterial.FlagCullDisabled }, new uint[] { 0, 1 });
            byte[] c = WmoSynthetic.ContextRoot(flagsB, 7400, 7500, null, new[] { 0u, WmoMaterialSemantics.FlagUnfogged, 0u }, new uint[] { 0, 0, 2 });
            WmoRoot root = WmoParser.ParseRoot(c, "fog flags");
            bool parsed = root != null && root.Materials.Length == 3 && root.Materials[0].Flags == 0 && root.Materials[0].Texture1 == 7500 &&
                          root.Materials[1].Flags == WmoMaterialSemantics.FlagUnfogged && root.Materials[1].BlendMode == 0 && root.Materials[1].Texture1 == 0 &&
                          root.Materials[2].BlendMode == 2 && root.GroupInfos.Length == 2;
            Check(ShaOf(a) == ContextRootSha256A && ShaOf(b) == ContextRootSha256B && System.Linq.Enumerable.SequenceEqual(a, a2) &&
                  System.Linq.Enumerable.SequenceEqual(b, b2) && parsed,
                  "R14 WmoSynthetic.ContextRoot: the old overload's bytes have HEAD's SHA-256 (" + ShaOf(a).Substring(0, 16) + ", " + ShaOf(b).Substring(0, 16) +
                  "); the new overload with HEAD's two materials gives the same bytes; three materials with F_UNFOGGED on the second and MOMT blend 2 " +
                  "on the third are read back by the parser");
        }

        // ================================================================================== run

        // ================================================================================== R15: the end rule after in-game test T7

        static void EndRuleTests()
        {
            // The rule at the measured far clip (1000) and the default (7000), at the measured horizonStarts (1000, 1900) and two
            // above them (2500; 3500, where the start cap 3000 binds): E and the start stated here (T7End, the cap 3000), the end
            // fade's zero at E.
            WorldLightSample basis = SampleAt(SetOf("wow", NoFlags, Day5985), 1427);
            double scaler = basis.Float(WorldLightColumns.FogScaler);
            var cells = new List<string>();
            bool rule = scaler > 0.1 && scaler < 0.2;
            foreach (double far in new[] { 1000.0, 7000.0 })
                foreach (double hs in new[] { 1000.0, 1900.0, 2500.0, 3500.0 })
                {
                    WorldFogRecord r = PackOf(basis.V, 1427, NoonSun, WorldFogMode.RetailPattern, NoFlags, far, WorldFog.PublicAll, hs);
                    double e = T7End(far, hs), start = scaler * Math.Min(e, 3000.0);
                    double vInside, vBeyond, hf;
                    WorldFog.Visibility(r, new[] { 0.99 * e, 0.0, 0.0 }, UpUnity, out vInside, out hf);
                    WorldFog.Visibility(r, new[] { e + 0.5, 0.0, 0.0 }, UpUnity, out vBeyond, out hf);
                    bool pinned = far == 1000.0 ? Math.Abs(e - 933.0) < 1e-9 : e == hs;
                    bool cap = hs == 3500.0 && far == 7000.0 ? r.R[0] == scaler * 3000.0 && r.R[0] < scaler * e : r.R[0] == scaler * e;
                    rule &= r.R[1] == e && r.R[0] == start && cap && pinned && r.EndRule == e && r.Start == r.R[0] && r.End == e && r.FarClip == far &&
                            r.HorizonStart == hs && r.R[2] == basis.Float(WorldLightColumns.FogDensity) * 0.0005 && vInside > 0.0 && vBeyond == 0.0 &&
                            !r.Artistic && r.Mode == WorldFogMode.RetailPattern;
                    cells.Add("F " + F(far) + " Hs " + F(hs) + ": E " + F(e, "0.##") + ", start " + F(r.R[0], "0.##"));
                }
            Check(rule,
                  "R15 T7's end rule on LightParams 5985 at 11:53:30 (FogScaler " + F(scaler, "0.####") + "): R0.y = E = min(0.933 x far clip, horizonStart), " +
                  "R0.x = FogScaler x min(E, 3000), the density FogDensity x 0.0005, visibility above 0 at 0.99 E and 0 beyond E -- " +
                  string.Join("; ", cells) + " (the cap binds at Hs 3500 only; the far clip's factor acts at F 1000 only)");

            // ASSUMED, as committed: the colour ramp's far clip, and the polynomial path's end row (per key: the far clip on a key
            // with the path, so a blend draws z E + (1 - z) F); the path's visibility is the committed record's exactly.
            WorldLightSample ramp = SampleAt(SetOf("wow", NoFlags, Synthetic[5]), 1440), poly = SampleAt(SetOf("wow", NoFlags, Synthetic[9]), 1440),
                              plain = SampleAt(SetOf("wow", NoFlags, Synthetic[0]), 1440);
            bool assumed = true;
            foreach (double far in new[] { 1000.0, 7000.0 })
                foreach (double hs in new[] { 1000.0, 1900.0, 3500.0 })
                {
                    WorldFogRecord rr = PackOf(ramp.V, 1440, NoonSun, WorldFogMode.RetailPattern, NoFlags, far, WorldFog.PublicAll, hs);
                    WorldFogRecord rp = PackOf(poly.V, 1440, NoonSun, WorldFogMode.RetailPattern, NoFlags, far, WorldFog.PublicAll, hs);
                    double e = T7End(far, hs);
                    assumed &= rr.R[27] == far && rr.R[1] == e && rp.R[50] == 0.0 && rp.Artistic && rp.R[1] == far && rp.EndRule == e &&
                               rp.R[0] == poly.Float(WorldLightColumns.FogScaler) * Math.Min(e, 3000.0);
                    // The record as committed before T7 (R0 = (min(F, 3000) x FogScaler, F)): with z 0 the path alone draws, so the
                    // visibility is the same at every distance.
                    var committed = new WorldFogRecord();
                    rp.CopyTo(committed);
                    committed.R[0] = Math.Min(far, 3000.0) * poly.Float(WorldLightColumns.FogScaler);
                    committed.R[1] = far;
                    for (double d = 0.0; d <= 9000.0; d += 50.0)
                        foreach (double h in new[] { -20.0, 0.0, 120.0 })
                        {
                            double va, vb, ha, hb;
                            WorldFog.Visibility(rp, new[] { d * 0.6, h, d * 0.8 }, UpUnity, out va, out ha);
                            WorldFog.Visibility(committed, new[] { d * 0.6, h, d * 0.8 }, UpUnity, out vb, out hb);
                            assumed &= va == vb && ha == hb;
                        }
                }
            var vh = new double[WorldLightSample.Count];
            for (int j = 0; j < vh.Length; j++)
                vh[j] = plain.V[j] + (poly.V[j] - plain.V[j]) * 0.5;
            WorldFogRecord half = PackOf(vh, 1440, NoonSun);
            double eHalf = T7End(Far, Hs);
            bool blend = half.R[50] == 0.5 && half.Artistic && Near(half.R[1], 0.5 * eHalf + 0.5 * Far, 1e-12) && half.EndRule == eHalf;
            Check(assumed && blend,
                  "R15 ASSUMED as committed: the colour ramp on EndFogColorDistance 0 reads the far clip (not E) and a key with the polynomial path keeps " +
                  "the end row at the far clip at every far clip and horizonStart -- its visibility equals the committed record's exactly (rays to 9000 " +
                  "yd at three heights) --; the start FogScaler x min(E, 3000) there too; a half blend of a plain and a polynomial key draws z E + " +
                  "(1 - z) F = " + F(half.R[1], "0.##") + " (z " + F(half.R[50]) + ")");

            // Classic Era, MoP Classic and Classic Beta stay unfogged by default; the arms do not read horizonStart (their records
            // are the same at every horizonStart, R0 as committed) and their texts do not name it.
            bool untouched = WorldFog.ModeFor("wow_classic_era", true, WorldFogClassicArm.Off) == WorldFogMode.Off &&
                             WorldFog.ModeFor("wow_classic", true, WorldFogClassicArm.Off) == WorldFogMode.Off &&
                             WorldFog.ModeFor("wow_classic_beta", true, WorldFogClassicArm.Off) == WorldFogMode.Off;
            foreach (WorldFogMode arm in new[] { WorldFogMode.ClassicPublic, WorldFogMode.ClassicPower36 })
            {
                WorldFogRecord at1900 = PackOf(plain.V, 1440, NoonSun, arm, NoFlags, Far, WorldFog.PublicAll, 1900.0);
                foreach (double hs in new[] { 1000.0, 2500.0, 3500.0 })
                {
                    WorldFogRecord other = PackOf(plain.V, 1440, NoonSun, arm, NoFlags, Far, WorldFog.PublicAll, hs);
                    for (int k = 0; k < 56; k++)
                        untouched &= other.R[k] == at1900.R[k];
                    untouched &= other.Start == at1900.Start && other.End == at1900.End && other.K == at1900.K && other.EndRule == Far;
                }
                untouched &= at1900.R[1] == Far && at1900.R[0] == Math.Min(Far, 3000.0) * plain.Float(WorldLightColumns.FogScaler) &&
                             !at1900.Summary().Contains("horizonStart");
            }
            WorldLightSwitches arms = WorldLightSwitches.Parse(new[] { "-wmvFogClassic=public", "-wmvGameHorizonStart=1000" }, "on");
            untouched &= !arms.DescribeObjects("wow_classic_era").Contains("horizonStart") && !arms.DescribeObjects("wow_classic").Contains("horizonStart") &&
                         !arms.DescribeObjects("wow_classic_beta").Contains("horizonStart");
            Check(untouched,
                  "R15 Classic Era, MoP Classic and Classic Beta untouched: unfogged by default; the public and power36 arms give the same record at " +
                  "horizonStart 1000, 1900, 2500 and 3500 (R0 as committed: min(F, 3000) x FogScaler and F) and no text of theirs names horizonStart");

            // -wmvGameHorizonStart: read once like -wmvGameFarclip, refused values are notes, a setting (not a draw switch).
            WorldLightSwitches byDefault = WorldLightSwitches.Parse(new string[0], null);
            WorldLightSwitches given =WorldLightSwitches.Parse(new[] { "-wmvGameHorizonStart=2500" }, "on");
            WorldLightSwitches last = WorldLightSwitches.Parse(new[] { "-wmvGameHorizonStart=1000", "-wmvGameHorizonStart=2500.5" }, "on");
            WorldLightSwitches far1000 = WorldLightSwitches.Parse(new[] { "-wmvGameFarclip=1000", "-wmvGameHorizonStart=2500" }, "on");
            WorldLightSwitches bad = WorldLightSwitches.Parse(new[] { "-wmvGameHorizonStart=0", "-wmvGameHorizonStart=-5", "-wmvGameHorizonStart=abc",
                                                                      "-wmvGameHorizonStart=NaN", "-wmvGameHorizonStart=Infinity", "-wmvGameHorizonStart=" }, "on");
            bool switchOk = byDefault.HorizonStart == 1900.0 && byDefault.HorizonStart == WorldLightModel.DefaultHorizonStart && byDefault.Notes.Count == 0 &&
                            byDefault.DrawDefaults &&
                            byDefault.Describe().Contains("; game horizonStart 1900 yd (the T7 reference sessions' value at graphicsViewDistance 6); ") &&
                            given.HorizonStart == 2500.0 && given.Notes.Count == 0 && given.DrawDefaults && given.FarClip == 7000.0 &&
                            given.Describe().Contains("; game horizonStart 2500 yd (-wmvGameHorizonStart); ") &&
                            given.DescribeObjects("wow").Contains("min(0.933 x 7000, 2500) = 2500 yd") && last.HorizonStart == 2500.5 &&
                            far1000.DescribeObjects("wow").Contains("min(0.933 x 1000, 2500) = 933 yd") &&
                            far1000.DescribeObjects("wow", null, 7000.0, 1000.0).Contains("min(0.933 x 7000, 1000) = 1000 yd") &&
                            bad.HorizonStart == 1900.0 && bad.Notes.Count == 6 &&
                            bad.Notes.TrueForAll(n => n.StartsWith("-wmvGameHorizonStart=", StringComparison.Ordinal) &&
                                                      n.EndsWith(" is not a number of yards above 0 -- 1900 is used", StringComparison.Ordinal)) && bad.DrawDefaults;
            Check(switchOk,
                  "R15 -wmvGameHorizonStart: 1900 by default (the T7 reference sessions' value, said so), 2500 taken, the last of two wins (2500.5), " +
                  "0, -5, abc, NaN, Infinity and an empty value refused as notes (\"" + (bad.Notes.Count > 0 ? bad.Notes[0] : "") + "\"), DrawDefaults " +
                  "kept (a setting, not a draw switch); the fog clause names F, Hs and E (\"" +
                  given.DescribeObjects("wow").Substring(Math.Max(0, given.DescribeObjects("wow").IndexOf("end E", StringComparison.Ordinal))) + "\")");

            // WMV_LIGHT=off: hard off whatever the switch says; nothing is on.
            WorldLightSwitches hard = WorldLightSwitches.Parse(new[] { "-wmvGameHorizonStart=1000", "-wmvWorldLight=on" }, "off");
            Check(hard.HardOff && !hard.On && hard.Describe().StartsWith("WMV_LIGHT=off: hard off (nothing evaluated or set: Stage 09 exactly); ", StringComparison.Ordinal),
                  "R15 WMV_LIGHT=off with -wmvGameHorizonStart=1000 and -wmvWorldLight=on: hard off, not on (the player evaluates, sets and says " +
                  "nothing: self-test F16)");
        }

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();
            log.Add("World fog: R1, R2 the client's terrain and M2 programs, re-executed offline");
            ProgramTests();
            log.Add("World fog: R3 Pack on the client's keys, the fall-backs and their blends");
            PackTests();
            log.Add("World fog: R4 the sky's shared tint inputs");
            TintTests();
            log.Add("World fog: R5 the modes and the arms");
            ModeTests();
            log.Add("World fog: R6 the twin's invariants");
            TwinTests();
            log.Add("World fog: R7 the switches");
            SwitchTests();
            log.Add("World fog: R8 the graded table");
            ModelTableTests();
            log.Add("World fog: R9 the order of the tint inputs (embedded days)");
            OrderTests();
            string keys = Environment.GetEnvironmentVariable("WMV_FOG_KEYS");
            log.Add("World fog: R9 the census on every key of five clients");
            if (string.IsNullOrEmpty(keys))
                log.Add("  SKIPPED: WMV_FOG_KEYS is not set (the key export is generated locally from the table extracts, not committed)");
            else
                CensusCorpusTests(keys);
            string fixtures = Environment.GetEnvironmentVariable("WMV_WORLD_LIGHT_FIXTURES");
            log.Add("World fog: R9 continuity on maps 0 and 1 of five clients");
            if (string.IsNullOrEmpty(fixtures))
                log.Add("  SKIPPED: WMV_WORLD_LIGHT_FIXTURES is not set (the fixtures are generated locally, not committed)");
            else
                ContinuityTests(fixtures);
            string parity = Environment.GetEnvironmentVariable("WMV_FOG_PARITY_OUT");
            log.Add("World fog: R10 the parity grid for the rig");
            if (string.IsNullOrEmpty(parity))
                log.Add("  SKIPPED: WMV_FOG_PARITY_OUT is not set");
            else
                ParityGrid(parity);
            log.Add("World fog: R11 the census on a synthetic set");
            CensusTests();
            log.Add("World fog: R12 no allocation");
            AllocationTests();
            log.Add("World fog: R13 the light sample's channels");
            SampleTests();
            log.Add("World fog: R14 the synthetic world model's materials");
            WmoSyntheticTests();
            log.Add("World fog: R15 the end rule after in-game test T7 and -wmvGameHorizonStart");
            EndRuleTests();
            log.Add(string.Format("WorldFogTests: {0} failure(s)", failures));
            if (output != null)
                foreach (string line in log)
                    output(line);
            return failures;
        }

        // ================================================================================== fixtures (generated)
        //
        // Written by D:\wmv-10\step7\impl\fixtures\make_fog_fixtures.py; do not edit by hand. Its inputs, by SHA-256:
        //   the Retail terrain program: D:\wmv-10\verify-bytecode\shapes\asm\wow\2977247\p_1.asm
        //     5b39df525091859b9dbe24c744bae17b048adfae36742e405be65fb0505ef3cd
        //   the M2 program combiners_opaque: D:\wmv-10\step6\m2\asm\wow\2977063\p_1.asm
        //     de53a133015452d630434ef9b5ebed0c9adaf0c9a500a72a6a78c453c8c71b7c
        //   the interpreter: D:\wmv-10\step7\understand\scripts\dxi.py
        //     de2a8b80f802d87a0397702a5cc8bb702bcdf5d154f8f2561a0dca43aab3a581
        //   the case builder: D:\wmv-10\step7\understand\scripts\fogcases.py
        //     751824c0fe4b18f79f301435013ddf7bc525d4af411bc51509a371da67ed0594
        //   the terrain harness: D:\wmv-10\step7\understand\scripts\run_fog_terrain.py
        //     e1858a0163fc5f2fb3e1a68ae3a19432a3e23298103e8ac60de3994ef8cb61fa
        //   the M2 harness: D:\wmv-10\step7\understand\scripts\run_fog_m2.py
        //     e1ed169eaaf21013e53864b2c00e7222eb5c301d5a48bd0db183f29a22a2f846
        //   the Python port: D:\wmv-10\step7\impl\fixtures\fogpack_ref.py
        //     e2277a7e34a249a5a82213a2cb970609058adb0a09d38b5e5f43c16cf47a196b
        //   this generator: D:\wmv-10\step7\impl\fixtures\make_fog_fixtures.py
        //     4e24ae824379fea946fc65b8b947a332825242ab81f31e8bb6c6009fe0622dc4
        //   the rig dump of Retail map 1: D:\wmv-10\checkpoint\rig\data\worldlight_wow_map1.json
        //     4a34c5c9f77f98753977a7cffdaa32dc144b07ee25173fab64868447f710a56a
        //   the key export (Retail): D:\wmv-10\step7\impl\fixtures\keys\fogkeys_wow.json
        //     a4486b95cf2ffdc9dd9d234f0b4efb2c32f6170944f3598998b4eeae06e8c2c4
        //   the Classic Era fixture of map 1: D:\wmv-10\step3\player\fixtures\wow_classic_era_map1.json
        //     a408490154d45258e0318651736273f8aac3a92ca0d00fd5c2ca599ba4b4cc49
        //   the MoP Classic fixture of map 1: D:\wmv-10\step3\player\fixtures\wow_classic_map1.json
        //     853dfa192c62af842d125ae2776bedf52f1625f6a01453bb83110aed44be1b13
        // R1 rows: "name regime|" then 70 float32 bit patterns: the 56 record values (row-major), up, p, the program's lit
        // output (its own fog skip) and its fog-on output. R2 rows add "|kind|" before them (blend word 0 -> kind 0, 4 -> 4).
        static readonly string[] TerrainProgramCases =
        {
            "sf_d20 before-start+tint|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 3db9b17a " +
                "00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 00000000 " +
                "00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 3f57a94e " +
                "3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 417093ed 415300cb 41c80000 3e1b213f 3e24eadd " +
                "3fbb6d52 3e99999a 3e1b2140 3e24eadd 3fbb6d52 3e99999a",
            "sf_d300 before-start|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 3db9b17a " +
                "00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 00000000 " +
                "00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 3f57a94e " +
                "3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 43221738 437c70f9 41c80000 3e22e2e8 3e24eadd " +
                "3fbb6d52 3e9eb852 3e22e2e8 3e24eadd 3fbb6d52 3e9eb852",
            "sf_d449 before-start|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 3db9b17a " +
                "00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 00000000 " +
                "00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 3f57a94e " +
                "3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 4337d213 43ccd2de 41c80000 3e2aa492 3e24eadd " +
                "3fbb6d52 3ea3d70a 3e2aa492 3e24eadd 3fbb6d52 3ea3d70a",
            "sf_d451 exponential|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 3db9b17a " +
                "00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 00000000 " +
                "00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 3f57a94e " +
                "3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 4337d0eb 43cdeb97 41c80000 3e32663c 3e24eadd " +
                "3fbb6d52 3ea8f5c3 3e3378d6 3e24fa8e 3fbb1e33 3ea8f5c3",
            "sf_d900 exponential|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 3db9b17a " +
                "00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 00000000 " +
                "00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 3f57a94e " +
                "3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 c1d23c8f 4460e771 41c80000 3e3a27e7 3e24eae2 " +
                "3fbb6d53 3eae147b 3ec31026 3e30ca37 3f7f1e7e 3eae147b",
            "sf_d2100 exponential+end-fade|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 " +
                "3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 " +
                "00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 " +
                "3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 c4f75557 442fde68 41c80000 3e423a79 " +
                "3e25b7a3 3fbb85a8 3eb33333 3f0dcdb2 3e3b2016 3f179456 3eb33333",
            "sf_d2101 exponential+end-fade|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 " +
                "3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 " +
                "00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 " +
                "3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 c4f789fc 442f7521 41c80000 3e49fcb3 " +
                "3e25b912 3fbb85d3 3eb851ec 3f0dfab4 3e3b20f6 3f178c7c 3eb851ec",
            "sf_d2500 exponential+end-fade|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 " +
                "3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 " +
                "00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 " +
                "3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 c51bfbcb c311ef74 41c80000 3e1f4180 " +
                "3e2f5c02 3fbcaafd 3ebd70a4 3f06ae5c 3e46a633 3f05624c 3ebd70a4",
            "sf_d3000 exponential+end-fade|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 " +
                "3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 " +
                "00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 " +
                "3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 c51f04cd c4c6b045 41c80000 3e3ab622 " +
                "3e6136a2 3fc297a7 3ec28f5c 3dc4b4b5 3e9a68c1 3df99019 3ec28f5c",
            "sf_d4899 exponential+end-fade+tint|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc " +
                "3e9b5769 3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a " +
                "00000000 00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 " +
                "3f800000 3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 456d4672 c54185f9 41c80000 " +
                "3e2aa492 3e24eadd 3fbb6d52 3ec7ae14 3e5c1992 3e8046d8 3e8b42f4 3ec7ae14",
            "sf_d5000 exponential+end-fade+tint|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc " +
                "3e9b5769 3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a " +
                "00000000 00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 " +
                "3f800000 3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 45826c83 c52c16d8 41c80000 " +
                "3e32663c 3e24eadd 3fbb6d52 3ecccccd 3e53308e 3e823b93 3e848349 3ecccccd",
            "sf_d6500 exponential+end-fade|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 " +
                "3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 " +
                "00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 " +
                "3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 45772489 45a136bb 41c80000 3e3a27e5 " +
                "3e24eadd 3fbb6d52 3ed1eb85 3dc0c90f 3e9b5546 3dba03ee 3ed1eb85",
            "sf_d6999 exponential+end-fade|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 " +
                "3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 " +
                "00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 " +
                "3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 44870c23 45d818e6 41c80000 3e41e990 " +
                "3e24eadf 3fbb6d52 3ed70a3d 3dc0c681 3e9b5666 3db9d87d 3ed70a3d",
            "sf_d7600 exponential+end-fade+beyond-end|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc " +
                "3e9b5769 3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a " +
                "00000000 00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 " +
                "3f800000 3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 c54ce76d 45d6440c 41c80000 " +
                "3e49ab84 3e24eb9c 3fbb6d69 3edc28f6 3dc0c3cc 3e9b5769 3db9b178 3edc28f6",
            "pull_0_0_40 before-start+pull|00000000 453b8000 3b449ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1b15579 3e6b2d8a 3e93fd0b 3d8fd157 3cc026ed 3b311760 3f58b9b4 3ec5e51a " +
                "3f754244 3f3b6706 3a0eda1b 3e56c029 3f690795 3ef0a22c 3f7af8ce 3ecb7b36 43b7fe7d 3f2123f5 00000000 " +
                "00000000 00000000 3f689c15 be74653f 3eaf7327 42a00000 00000000 00000000 00000000 3f800000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45997975 430a7e67 3f800000 3f800000 " +
                "3e7c4d1f 3dcef15e 3d7552c9 439f6780 00000000 00000000 3f800000 00000000 00000000 42200000 3e1b213f " +
                "3e24eadd 3fbb6d52 3ee147ae 3e1b213f 3e24eadd 3fbb6d52 3ee147ae",
            "pull_0_0_300 exponential+pull|00000000 453b8000 3b449ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1b15579 3e6b2d8a 3e93fd0b 3d8fd157 3cc026ed 3b311760 3f58b9b4 3ec5e51a " +
                "3f754244 3f3b6706 3a0eda1b 3e56c029 3f690795 3ef0a22c 3f7af8ce 3ecb7b36 43b7fe7d 3f2123f5 00000000 " +
                "00000000 00000000 3f689c15 be74653f 3eaf7327 42a00000 00000000 00000000 00000000 3f800000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45997975 430a7e67 3f800000 3f800000 " +
                "3e7c4d1f 3dcef15e 3d7552c9 439f6780 00000000 00000000 3f800000 00000000 00000000 43960000 3e22e2e8 " +
                "3e24eadd 3fbb6d52 3ee66666 3e45d05d 3e643d18 3f4a6df3 3ee66666",
            "pull_0_0_-300 exponential+pull|00000000 453b8000 3b449ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1b15579 3e6b2d8a 3e93fd0b 3d8fd157 3cc026ed 3b311760 3f58b9b4 3ec5e51a " +
                "3f754244 3f3b6706 3a0eda1b 3e56c029 3f690795 3ef0a22c 3f7af8ce 3ecb7b36 43b7fe7d 3f2123f5 00000000 " +
                "00000000 00000000 3f689c15 be74653f 3eaf7327 42a00000 00000000 00000000 00000000 3f800000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45997975 430a7e67 3f800000 3f800000 " +
                "3e7c4d1f 3dcef15e 3d7552c9 439f6780 00000000 00000000 3f800000 00000000 00000000 c3960000 3e3623c8 " +
                "3e420378 3fbee27d 3eeb851f 3e4fc3db 3e7346ea 3f4e00f3 3eeb851f",
            "pull_200_0_150 exponential+pull+tint|00000000 453b8000 3b449ba6 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c1b15579 3e6b2d8a 3e93fd0b 3d8fd157 3cc026ed 3b311760 3f58b9b4 " +
                "3ec5e51a 3f754244 3f3b6706 3a0eda1b 3e56c029 3f690795 3ef0a22c 3f7af8ce 3ecb7b36 43b7fe7d 3f2123f5 " +
                "00000000 00000000 00000000 3f689c15 be74653f 3eaf7327 42a00000 00000000 00000000 00000000 3f800000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 45997975 430a7e67 3f800000 " +
                "3f800000 3e7c4d1f 3dcef15e 3d7552c9 439f6780 00000000 00000000 3f800000 43480000 00000000 43160000 " +
                "3e32663c 3e24eadd 3fbb6d52 3ef0a3d7 3e3004ac 3e588006 3f69a246 3ef0a3d7",
            "pull_200_0_-150 exponential+pull|00000000 453b8000 3b449ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1b15579 3e6b2d8a 3e93fd0b 3d8fd157 3cc026ed 3b311760 3f58b9b4 3ec5e51a " +
                "3f754244 3f3b6706 3a0eda1b 3e56c029 3f690795 3ef0a22c 3f7af8ce 3ecb7b36 43b7fe7d 3f2123f5 00000000 " +
                "00000000 00000000 3f689c15 be74653f 3eaf7327 42a00000 00000000 00000000 00000000 3f800000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45997975 430a7e67 3f800000 3f800000 " +
                "3e7c4d1f 3dcef15e 3d7552c9 439f6780 00000000 00000000 3f800000 43480000 00000000 c3160000 3e3a2909 " +
                "3e24edc0 3fbb6daa 3ef5c28f 3e513838 3e6293df 3f4ef85d 3ef5c28f",
            "pull_500_300_79 exponential+pull|00000000 453b8000 3b449ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1b15579 3e6b2d8a 3e93fd0b 3d8fd157 3cc026ed 3b311760 3f58b9b4 3ec5e51a " +
                "3f754244 3f3b6706 3a0eda1b 3e56c029 3f690795 3ef0a22c 3f7af8ce 3ecb7b36 43b7fe7d 3f2123f5 00000000 " +
                "00000000 00000000 3f689c15 be74653f 3eaf7327 42a00000 00000000 00000000 00000000 3f800000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45997975 430a7e67 3f800000 3f800000 " +
                "3e7c4d1f 3dcef15e 3d7552c9 439f6780 00000000 00000000 3f800000 43fa0000 43960000 429e0000 3e41e98f " +
                "3e24eadd 3fbb6d52 3efae148 3ed8a069 3e98ced3 3f19e1fd 3efae148",
            "pull_10_10_-81 exponential+pull|00000000 453b8000 3b449ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1b15579 3e6b2d8a 3e93fd0b 3d8fd157 3cc026ed 3b311760 3f58b9b4 3ec5e51a " +
                "3f754244 3f3b6706 3a0eda1b 3e56c029 3f690795 3ef0a22c 3f7af8ce 3ecb7b36 43b7fe7d 3f2123f5 00000000 " +
                "00000000 00000000 3f689c15 be74653f 3eaf7327 42a00000 00000000 00000000 00000000 3f800000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45997975 430a7e67 3f800000 3f800000 " +
                "3e7c4d1f 3dcef15e 3d7552c9 439f6780 00000000 00000000 3f800000 41200000 41200000 c2a20000 3e4ea1f9 " +
                "3e317adc 3fbceb7f 3f000000 3e4fd243 3e366a08 3fb56d70 3f000000",
            "height0 exponential+hf+pull+w|42fc7410 45047c46 3abd4e14 426905e2 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c273b9ea 3e9bb8ea 3f628685 3e57c46c 3c82e529 3baa5cfc 3f244f57 3dcd7b42 " +
                "3f7d42e0 3d0d96de 3e843cf5 3f45cefe 3ea86cd8 3e97b7e1 3d9651fb 3db88f5a 44c08948 3e78d861 00000000 " +
                "00000000 00000000 bf6e8a4c 3eb9caf7 bc0c85dc 42904ee1 bea5075f 3d72df76 3f0ed526 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 457f5921 442ef93b 3f800000 3f800000 " +
                "3e3b3730 3e1dd5a6 3f688e75 43a38f76 00000000 00000000 3f800000 43650378 441013fc 42bf8a1d 3e1b213f " +
                "3e24eadd 3fbb6d52 3f028f5c 3e82b6cf 3e7f2319 3efabed3 3f028f5c",
            "height0 exponential+end-fade+beyond-end+pull+w+tint|42fc7410 45047c46 3abd4e14 426905e2 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 3f800000 c273b9ea 3e9bb8ea 3f628685 3e57c46c 3c82e529 3baa5cfc " +
                "3f244f57 3dcd7b42 3f7d42e0 3d0d96de 3e843cf5 3f45cefe 3ea86cd8 3e97b7e1 3d9651fb 3db88f5a 44c08948 " +
                "3e78d861 00000000 00000000 00000000 bf6e8a4c 3eb9caf7 bc0c85dc 42904ee1 bea5075f 3d72df76 3f0ed526 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 457f5921 442ef93b " +
                "3f800000 3f800000 3e3b3730 3e1dd5a6 3f688e75 43a38f76 00000000 00000000 3f800000 4441043d 450a5a11 " +
                "43340de9 3e22e2e8 3e24eadd 3fbb6d52 3f051eb8 3e3b3730 3e1dd5a6 3f688e75 3f051eb8",
            "height0 exponential+end-fade+beyond-end+hf+pull+w+tint|42fc7410 45047c46 3abd4e14 426905e2 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 3f800000 c273b9ea 3e9bb8ea 3f628685 3e57c46c 3c82e529 " +
                "3baa5cfc 3f244f57 3dcd7b42 3f7d42e0 3d0d96de 3e843cf5 3f45cefe 3ea86cd8 3e97b7e1 3d9651fb 3db88f5a " +
                "44c08948 3e78d861 00000000 00000000 00000000 bf6e8a4c 3eb9caf7 bc0c85dc 42904ee1 bea5075f 3d72df76 " +
                "3f0ed526 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 457f5921 " +
                "442ef93b 3f800000 3f800000 3e3b3730 3e1dd5a6 3f688e75 43a38f76 00000000 00000000 3f800000 c4db6aa5 " +
                "c4a6fbb1 c1fb563e 3e43cf34 3e649bb8 3fc2feef 3f07ae14 3ea1f527 3e21e155 3f69d681 3f07ae14",
            "height1 exponential+end-fade+hf+pull+w|42b00fab 44ebf5fc 3b45fff1 40840cd7 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c260a31a 3f0f32a2 3f5a36e1 3f1d42f6 3c7f13d4 3bb5b6de 3e50dfd7 " +
                "3c87c7dc 3e89d3c9 3e949f11 3d77a044 3e347bdf 3ebcd170 3f1279b2 3e06bc85 3eb96b17 450c929a 3f7b019e " +
                "00000000 00000000 00000000 bf0b50c2 3f3993f6 3ed838e7 429da9e8 becad586 beec2ffa 3f785b70 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 44c590c4 44372a33 3f800000 " +
                "3f800000 3f3372c5 3f767829 3cae27ba 437e794c 00000000 00000000 3f800000 c3136873 c4959ec9 c290e05c " +
                "3e3278eb 3e251a25 3fbb72f0 3f0a3d71 3f1d14df 3f042c9f 3e8851ef 3f0a3d71",
            "height1 exponential+end-fade+beyond-end+hf+pull+w|42b00fab 44ebf5fc 3b45fff1 40840cd7 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 3f800000 c260a31a 3f0f32a2 3f5a36e1 3f1d42f6 3c7f13d4 3bb5b6de " +
                "3e50dfd7 3c87c7dc 3e89d3c9 3e949f11 3d77a044 3e347bdf 3ebcd170 3f1279b2 3e06bc85 3eb96b17 450c929a " +
                "3f7b019e 00000000 00000000 00000000 bf0b50c2 3f3993f6 3ed838e7 429da9e8 becad586 beec2ffa 3f785b70 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 44c590c4 44372a33 " +
                "3f800000 3f800000 3f3372c5 3f767829 3cae27ba 437e794c 00000000 00000000 3f800000 450b026b 448e3c96 " +
                "41938151 3e3a27e5 3e24eadd 3fbb6d52 3f0ccccd 3f251d7f 3f5ae73e 3d4d81a8 3f0ccccd",
            "height2 exponential+end-fade+pull+w|436d28dc 45c04c3f 3b242803 4215fd93 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c2334430 3f152a72 3f1bdeb4 3da440f6 3c7a4d2d 3bc3678d 3f613a0a " +
                "3f3a6bc7 3ec6e120 3f1bc968 3f14b954 3ee18c2a 3f569f6b 3dab9604 3f400dc6 3cf409cd 44c5de98 3ef63ff9 " +
                "00000000 00000000 00000000 3f37b235 3f2a11ba 3e5638cf 41dd0345 3e509446 3e96270d 3f6e97b0 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 4525b3c7 4269ebef 3f800000 " +
                "3f800000 3e9a20f3 3f2d9a63 3e4f6fab 4287af8c 00000000 00000000 3f800000 c497b2ad c4ef012a c1b9d0e9 " +
                "3e4ee458 3e3222d4 3fbcff75 3f11eb85 3f600a1e 3f39733d 3ecab102 3f11eb85",
            "height2 exponential+pull+w|436d28dc 45c04c3f 3b242803 4215fd93 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c2334430 3f152a72 3f1bdeb4 3da440f6 3c7a4d2d 3bc3678d 3f613a0a 3f3a6bc7 " +
                "3ec6e120 3f1bc968 3f14b954 3ee18c2a 3f569f6b 3dab9604 3f400dc6 3cf409cd 44c5de98 3ef63ff9 00000000 " +
                "00000000 00000000 3f37b235 3f2a11ba 3e5638cf 41dd0345 3e509446 3e96270d 3f6e97b0 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 4525b3c7 4269ebef 3f800000 3f800000 " +
                "3e9a20f3 3f2d9a63 3e4f6fab 4287af8c 00000000 00000000 3f800000 c3dc0768 434c0f56 42f4ca6d 3e22e479 " +
                "3e24eed4 3fbb6dcb 3f170a3d 3e16f92b 3f13a810 3ee72e64 3f170a3d",
            "height4 before-start+hf+pull+w|4362dcb0 454a7ccf 3b566b12 42597f33 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c2530edb 3f6ebf20 3f38edb6 3e05071b 3c26e392 3b811f6f 3f68f37d 3ec0ec4b " +
                "3f119dcc 3f446ab0 3f4bf8f0 3f71bae5 3eed6b1a 3f26bd13 3e51cfd4 3f38d0c6 4502231e 3f2440f7 00000000 " +
                "00000000 00000000 3f353c32 3d96124e 3f33d3d7 42ac3d27 bce3bdde 3e5b4fa2 3eafc245 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 4574c4e0 4420c5c8 3f800000 3f800000 " +
                "3ea40ac7 3f68f516 3f5b14a0 430b671f 00000000 00000000 3f800000 c3443541 4298cdcd 41a0f757 3e420238 " +
                "3e252946 3fbb74bd 3f2147ae 3e420238 3e252946 3fbb74bd 3f2147ae",
            "height5 exponential+hf+pull+w+tint|434ecfbc 45d03d12 3b11c11c 4263cc0b 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c283e664 3e62ba65 3f06d36a 3e94911f 3beceac1 3b83861f 3f05d517 " +
                "3f57f7b4 3f0f5a44 3e124bcf 3ec32edf 3f5862ff 3f6688cf 3e553cc1 3f59cc25 3f77ebb5 44afb708 3f12af6a " +
                "00000000 00000000 00000000 3d3a4480 3f1d805d 3f497a9d 41c0edb5 bea77a23 be28f4a0 3f549029 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 45b73293 43da81b5 3f800000 " +
                "3f800000 3ecd1956 3f4d12d4 3f101808 43446a8b 00000000 00000000 3f800000 44c5dabf 442de70a 4177c761 " +
                "3e22e2e8 3e24eadd 3fbb6d52 3f28f5c3 3ee00267 3f4e4b8b 3f138dc3 3f28f5c3",
            "art0 exponential+end-fade+beyond-end+poly0.00+pull+w|430ef5b6 452cebb1 3a995d67 42144b76 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 3f800000 424c2143 3e04918b 3f477f79 3cbaa994 3bf3a12c " +
                "3ae55dc8 3f2fe176 3ea4e7f7 3eb5f00e 3ef6a704 3dd6ce36 3f3b1bda 3dfb75d6 3f02ae1a 3e804891 3e4a796e " +
                "44b179e6 3edfa11b 00000000 00000000 00000000 be17bddf 3f3495dd 3f317143 42345a77 be315a49 3d706a96 " +
                "bf2e3811 bf176a6a bf4d5180 3f38e99e 3f6e7868 3e38de4d bf0d68af bf0e16c1 3fa257af 3d32b42e 4587a757 " +
                "4404366d 00000000 3f800000 3f07951d 3f59ec0e 3f1c991a 43ab5a54 00000000 00000000 3f800000 45308141 " +
                "454584a9 c25085a3 3e3a27e5 3e24eadd 3fbb6d52 3f30a3d7 3f07951d 3f59ec0e 3f1c991a 3f30a3d7",
            "art0 exponential+end-fade+beyond-end+poly0.00+pull+w+tint|430ef5b6 452cebb1 3a995d67 42144b76 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 3f800000 424c2143 3e04918b 3f477f79 3cbaa994 3bf3a12c " +
                "3ae55dc8 3f2fe176 3ea4e7f7 3eb5f00e 3ef6a704 3dd6ce36 3f3b1bda 3dfb75d6 3f02ae1a 3e804891 3e4a796e " +
                "44b179e6 3edfa11b 00000000 00000000 00000000 be17bddf 3f3495dd 3f317143 42345a77 be315a49 3d706a96 " +
                "bf2e3811 bf176a6a bf4d5180 3f38e99e 3f6e7868 3e38de4d bf0d68af bf0e16c1 3fa257af 3d32b42e 4587a757 " +
                "4404366d 00000000 3f800000 3f07951d 3f59ec0e 3f1c991a 43ab5a54 00000000 00000000 3f800000 c34ef046 " +
                "45587aa1 c2a13193 3e41e995 3e24eaed 3fbb6d54 3f333333 3f043188 3f58f631 3f18b265 3f333333",
            "art0 exponential+end-fade+beyond-end+poly0.00+pull+w|430ef5b6 452cebb1 3a995d67 42144b76 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 3f800000 424c2143 3e04918b 3f477f79 3cbaa994 3bf3a12c " +
                "3ae55dc8 3f2fe176 3ea4e7f7 3eb5f00e 3ef6a704 3dd6ce36 3f3b1bda 3dfb75d6 3f02ae1a 3e804891 3e4a796e " +
                "44b179e6 3edfa11b 00000000 00000000 00000000 be17bddf 3f3495dd 3f317143 42345a77 be315a49 3d706a96 " +
                "bf2e3811 bf176a6a bf4d5180 3f38e99e 3f6e7868 3e38de4d bf0d68af bf0e16c1 3fa257af 3d32b42e 4587a757 " +
                "4404366d 00000000 3f800000 3f07951d 3f59ec0e 3f1c991a 43ab5a54 00000000 00000000 3f800000 c55ade64 " +
                "44ead72b 4267f2db 3e49c658 3e252f82 3fbb757a 3f35c28f 3f07951d 3f59ec0e 3f1c991a 3f35c28f",
            "art1 exponential+poly0.00+pull+w+tint|40aa7bb9 44ffb951 3b3d4a52 425abac1 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 4260d252 3dec5c35 3f01744e 3f421669 3c83b5fe 3b8bf5e1 3e418b20 " +
                "3d907da2 3dd97166 be22a1da 3f0d3a58 3f03cb87 3f119916 3e161278 3e3cf715 3e50ced6 450547e3 3f7d8464 " +
                "00000000 00000000 00000000 bf5655b7 be5eddf9 3f006c0b 42de7444 bf4f3bdc bf6048bc 3f672a58 bd9b5bb6 " +
                "bfbae9dd 3f1a6420 3f84326b 3d147ee7 be0d766c 3f37cf07 be9174bc 3d1fd8de 459a69f0 43938f6b 00000000 " +
                "3f800000 3eef155d 3f03e9db 3edc339c 43705f36 00000000 00000000 3f800000 43085f84 c3fa2df3 c20d0aea " +
                "3e1b2164 3e24eb3a 3fbb6d5d 3f3851ec 3e1c2e33 3e26c979 3fb72d08 3f3851ec",
            "art2 before-start+poly0.00+pull+w+tint|4364b7eb 451e4ff7 3a6dc0ff 42325086 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c2232ea9 3f6f0d3c 3e086987 3ea8ea0e 3c8bfd1e 3b261612 3edd0069 " +
                "3e39e820 3e53caec 3b5836df 3f4c1e04 3e9a7709 3dce915c 3f635720 3f7d9697 3f6717a6 439d015a 3e8bc373 " +
                "00000000 00000000 00000000 bf6e667d 3e828add 3e8547e4 42e6bdeb becdf5f9 bf11622e be403e3b bf22d5f6 " +
                "3e66221a bf93edc9 bf968186 3d422d05 be2d1e36 3c6a06d9 3f48aeb4 3e04b65d 44e6b817 43da0305 00000000 " +
                "3f800000 3efec4c5 3e5a5604 3df4f653 431e2659 00000000 00000000 3f800000 bf85e581 433b9918 42a241df " +
                "3e32663c 3e24eadd 3fbb6d52 3f400000 3e84ba48 3e817f81 3fb01373 3f400000",
            "art2 exponential+end-fade+poly0.00+pull+w+tint|4364b7eb 451e4ff7 3a6dc0ff 42325086 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 3f800000 c2232ea9 3f6f0d3c 3e086987 3ea8ea0e 3c8bfd1e 3b261612 " +
                "3edd0069 3e39e820 3e53caec 3b5836df 3f4c1e04 3e9a7709 3dce915c 3f635720 3f7d9697 3f6717a6 439d015a " +
                "3e8bc373 00000000 00000000 00000000 bf6e667d 3e828add 3e8547e4 42e6bdeb becdf5f9 bf11622e be403e3b " +
                "bf22d5f6 3e66221a bf93edc9 bf968186 3d422d05 be2d1e36 3c6a06d9 3f48aeb4 3e04b65d 44e6b817 43da0305 " +
                "00000000 3f800000 3efec4c5 3e5a5604 3df4f653 431e2659 00000000 00000000 3f800000 c48b396f c25b1030 " +
                "42badac1 3e3c4457 3e2a429b 3fbc0fdc 3f428f5c 3ebdff1c 3e4bdd6d 3f4dba08 3f428f5c",
            "art2 exponential+end-fade+poly0.00+pull+w|4364b7eb 451e4ff7 3a6dc0ff 42325086 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c2232ea9 3f6f0d3c 3e086987 3ea8ea0e 3c8bfd1e 3b261612 3edd0069 " +
                "3e39e820 3e53caec 3b5836df 3f4c1e04 3e9a7709 3dce915c 3f635720 3f7d9697 3f6717a6 439d015a 3e8bc373 " +
                "00000000 00000000 00000000 bf6e667d 3e828add 3e8547e4 42e6bdeb becdf5f9 bf11622e be403e3b bf22d5f6 " +
                "3e66221a bf93edc9 bf968186 3d422d05 be2d1e36 3c6a06d9 3f48aeb4 3e04b65d 44e6b817 43da0305 00000000 " +
                "3f800000 3efec4c5 3e5a5604 3df4f653 431e2659 00000000 00000000 3f800000 44904ef1 c20e117c 41dcd1f6 " +
                "3e41e98f 3e24eadd 3fbb6d52 3f451eb8 3eb18d1a 3e4032c8 3f470dae 3f451eb8",
            "art3 exponential+end-fade+beyond-end+poly0.00+hf+pull+w|4363c643 4486f68b 3b761f7b 411988c9 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 3f800000 c058df16 3e2d2e7d 3efdb4ea 3f1c75be 3b6e80ce " +
                "3bbabeb5 3ed7667f 3f06d9a1 3f190b85 3e4efbeb 3e924de1 3f27b564 3f0f8662 3e912aa5 3f377337 3e97929f " +
                "43683a8b 3e7ae743 00000000 00000000 00000000 3f0e0e53 bf428088 3ead7e7b 40a44090 bf2fd3c6 3f0260c4 " +
                "be615efb 3f4b8aa4 be588714 bd8992df 3fb5b250 3d47a556 3d8f8539 3fa7e97c 3f2b1007 3dbfd618 45981d12 " +
                "42af3cc1 00000000 3f800000 3f7d1fff 3f71c7f5 3d96896a 43b51c34 00000000 00000000 3f800000 451788cb " +
                "4505c0df 41c6cbf8 3e49ab38 3e24eadd 3fbb6d52 3f47ae14 3f3651c0 3f3db7b8 3ea85799 3f47ae14",
            "art5 before-start+poly0.35+hf+pull+w|42ae880a 45ac0da9 3b5e44c2 4130833f 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 4193a777 3f14a185 3eec8d65 3ea5f776 3c8d1493 3b3c7a35 3f116659 " +
                "3f50b78c 3eed517d 3f34b251 3c184021 3e9ecb6e 3f692296 3f1a8d54 3f31f267 3efd5159 44f4b15a 3ea7e8fa " +
                "00000000 00000000 00000000 bf2b1b29 3f26132b 3eba4f54 42d39c4f bf0fffa6 3ea10998 beb115a2 3f50038d " +
                "3db99e2b 3fbd1182 be002d32 3d3537d0 3fb39fa7 3f307a13 3d46a5eb 3c8a1894 4538700e 43be5e25 3eb33333 " +
                "3f800000 3f1842be 3f670301 3f2fe294 42df5c93 00000000 00000000 3f800000 c2102fb9 c292022e 4187b1a2 " +
                "3e4248c2 3e25dbcb 3fbb89f4 3eae147b 3e4bc540 3e2eb894 3fb84bb7 3eae147b",
            "art5 exponential+end-fade+poly0.35+hf+pull+w|42ae880a 45ac0da9 3b5e44c2 4130833f 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 3f800000 4193a777 3f14a185 3eec8d65 3ea5f776 3c8d1493 3b3c7a35 " +
                "3f116659 3f50b78c 3eed517d 3f34b251 3c184021 3e9ecb6e 3f692296 3f1a8d54 3f31f267 3efd5159 44f4b15a " +
                "3ea7e8fa 00000000 00000000 00000000 bf2b1b29 3f26132b 3eba4f54 42d39c4f bf0fffa6 3ea10998 beb115a2 " +
                "3f50038d 3db99e2b 3fbd1182 be002d32 3d3537d0 3fb39fa7 3f307a13 3d46a5eb 3c8a1894 4538700e 43be5e25 " +
                "3eb33333 3f800000 3f1842be 3f670301 3f2fe294 42df5c93 00000000 00000000 3f800000 c3353fdc c5212d86 " +
                "c1d2cdd9 3e49b035 3e24f77d 3fbb6ed2 3eb33333 3f16bca8 3f6217c2 3f236ae5 3eb33333",
            "art5 exponential+poly0.35+hf+pull+w|42ae880a 45ac0da9 3b5e44c2 4130833f 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 4193a777 3f14a185 3eec8d65 3ea5f776 3c8d1493 3b3c7a35 3f116659 " +
                "3f50b78c 3eed517d 3f34b251 3c184021 3e9ecb6e 3f692296 3f1a8d54 3f31f267 3efd5159 44f4b15a 3ea7e8fa " +
                "00000000 00000000 00000000 bf2b1b29 3f26132b 3eba4f54 42d39c4f bf0fffa6 3ea10998 beb115a2 3f50038d " +
                "3db99e2b 3fbd1182 be002d32 3d3537d0 3fb39fa7 3f307a13 3d46a5eb 3c8a1894 4538700e 43be5e25 3eb33333 " +
                "3f800000 3f1842be 3f670301 3f2fe294 42df5c93 00000000 00000000 3f800000 4432c554 448325de 4191d377 " +
                "3e1b213f 3e24eadd 3fbb6d52 3eb851ec 3eb3124f 3ebe2551 3f7d7c4b 3eb851ec",
            "power0_0.1 power-inside|438034f6 45b64bdd 3aa707c6 405dac83 3f800000 44823948 3a82a5ae 3fd9999a 00000000 " +
                "00000000 3f800000 c223d4e3 3e977d7a 3cb702db 3f5778db 3c62b400 3b7288cd 3e9c672d 3f0ba2f3 3eab465b " +
                "3dcedda1 3dd7dee0 3ea0ab56 3cd4a9bb 3f274e4b 3dc79390 3f50f5e2 44925706 3f3652f1 00000000 00000000 " +
                "00000000 bf1975c0 bf43179c 3e7a99f2 426ab967 3f50c2ec bea5b73a bda7a61d be992625 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 45a065eb 4395dcc7 3f800000 3f800000 3e0bff27 " +
                "3ef9e088 3f4e062c 41e7e636 00000000 00000000 3f800000 427a0794 42a6afb8 00000000 3e22e2e8 3e24eadd " +
                "3fbb6d52 3ebd70a4 3e321c15 3e157f8c 3fb2c404 3ebd70a4",
            "power0_0.5 power-inside|438034f6 45b64bdd 3aa707c6 405dac83 3f800000 44823948 3a82a5ae 3fd9999a 00000000 " +
                "00000000 3f800000 c223d4e3 3e977d7a 3cb702db 3f5778db 3c62b400 3b7288cd 3e9c672d 3f0ba2f3 3eab465b " +
                "3dcedda1 3dd7dee0 3ea0ab56 3cd4a9bb 3f274e4b 3dc79390 3f50f5e2 44925706 3f3652f1 00000000 00000000 " +
                "00000000 bf1975c0 bf43179c 3e7a99f2 426ab967 3f50c2ec bea5b73a bda7a61d be992625 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 45a065eb 4395dcc7 3f800000 3f800000 3e0bff27 " +
                "3ef9e088 3f4e062c 41e7e636 00000000 00000000 3f800000 439c44bd 43d05ba6 00000000 3e2aa492 3e24eadd " +
                "3fbb6d52 3ec28f5c 3e82057c 3dc05e82 3f82a6d3 3ec28f5c",
            "power0_0.9 power-inside|438034f6 45b64bdd 3aa707c6 405dac83 3f800000 44823948 3a82a5ae 3fd9999a 00000000 " +
                "00000000 3f800000 c223d4e3 3e977d7a 3cb702db 3f5778db 3c62b400 3b7288cd 3e9c672d 3f0ba2f3 3eab465b " +
                "3dcedda1 3dd7dee0 3ea0ab56 3cd4a9bb 3f274e4b 3dc79390 3f50f5e2 44925706 3f3652f1 00000000 00000000 " +
                "00000000 bf1975c0 bf43179c 3e7a99f2 426ab967 3f50c2ec bea5b73a bda7a61d be992625 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 45a065eb 4395dcc7 3f800000 3f800000 3e0bff27 " +
                "3ef9e088 3f4e062c 41e7e636 00000000 00000000 3f800000 440ca443 443b85af 00000000 3e32663c 3e24eadd " +
                "3fbb6d52 3ec7ae14 3e9869bc 3e878063 3f1f745a 3ec7ae14",
            "power0_1.2 power-beyond|438034f6 45b64bdd 3aa707c6 405dac83 3f800000 44823948 3a82a5ae 3fd9999a 00000000 " +
                "00000000 3f800000 c223d4e3 3e977d7a 3cb702db 3f5778db 3c62b400 3b7288cd 3e9c672d 3f0ba2f3 3eab465b " +
                "3dcedda1 3dd7dee0 3ea0ab56 3cd4a9bb 3f274e4b 3dc79390 3f50f5e2 44925706 3f3652f1 00000000 00000000 " +
                "00000000 bf1975c0 bf43179c 3e7a99f2 426ab967 3f50c2ec bea5b73a bda7a61d be992625 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 45a065eb 4395dcc7 3f800000 3f800000 3e0bff27 " +
                "3ef9e088 3f4e062c 41e7e636 00000000 00000000 3f800000 443b85af 447a0794 00000000 3e3a27e5 3e24eadd " +
                "3fbb6d52 3ecccccd 3e9c672d 3f0ba2f3 3eab465a 3ecccccd",
            "power1_0.1 power-before|43854c0b 45d29239 3b33a1a0 41fb7372 3f800000 447d6f92 3a9d8459 3f800000 00000000 " +
                "00000000 3f800000 41c9aed6 3f4d607b 3ed7dd63 3ed74617 3bc7aa57 3b95f8ca 3f7db2db 3ec04475 3e2baddf " +
                "3ccb2bf0 3ed9926a 3e958438 3f784236 3d72830f 3e9dec73 3deb22ed 44d34b5a 3f46a3ae 00000000 00000000 " +
                "00000000 3f2584bc 3f0f3b8d 3f04c16f 41ac416c bf601461 bda8ebc6 3e2c2b05 3f518f1b 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 44cff5f3 430381c6 3f800000 3f800000 3e3cf814 " +
                "3e5e94c2 3e710f00 438f72e1 00000000 00000000 3f800000 42734c63 42a232ed 00000000 3e41e98f 3e24eadd " +
                "3fbb6d52 3ed1eb85 3e41e990 3e24eadd 3fbb6d52 3ed1eb85",
            "sun_a-0.2 exponential+pull+tint|00000000 457a0000 3b03126f 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c19802d7 3f52c0fd 3f6d491c 3e0e6424 3bfe0a47 3aac35a6 3f30fa67 3ef72a40 " +
                "3f6bdbee be4ccccd 3f3a18a2 3f1cf3cc 3ea2a9f0 3f49ebe0 3e7aeebd 3f7c4cc9 44b1580f 3f333333 00000000 " +
                "00000000 00000000 3f70d315 3e40a8de 3e907ea6 41b535a5 00000000 00000000 00000000 3f800000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 453b565f 43870dd1 3f800000 3f800000 " +
                "3dd24116 3f36a388 3e81fc6a 43578906 00000000 00000000 3f800000 44610000 43340000 43870000 3e1b213f " +
                "3e24eadd 3fbb6d52 3f11eb85 3f2e20df 3f2c1c6c 3ee8fa88 3f11eb85",
            "sun_a-0.2 exponential+tint|00000000 457a0000 3b03126f 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c19802d7 3f52c0fd 3f6d491c 3e0e6424 3bfe0a47 3aac35a6 3f30fa67 3ef72a40 " +
                "3f6bdbee be4ccccd 3f3a18a2 3f1cf3cc 3ea2a9f0 3f49ebe0 3e7aeebd 3f7c4cc9 44b1580f 3f333333 00000000 " +
                "00000000 00000000 3f70d315 3e40a8de 3e907ea6 41b535a5 00000000 00000000 00000000 3f800000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 453b565f 43870dd1 3f800000 3f800000 " +
                "3dd24116 3f36a388 3e81fc6a 43578906 00000000 00000000 3f800000 44610000 c3960000 00000000 3e22e2e8 " +
                "3e24eadd 3fbb6d52 3f147ae1 3f2e685d 3f2c49a9 3ee864a4 3f147ae1",
            "sun_a-0.2 exponential+pull+tint|00000000 457a0000 3b03126f 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c19802d7 3f52c0fd 3f6d491c 3e0e6424 3bfe0a47 3aac35a6 3f30fa67 3ef72a40 " +
                "3f6bdbee be4ccccd 3f3a18a2 3f1cf3cc 3ea2a9f0 3f49ebe0 3e7aeebd 3f7c4cc9 44b1580f 3f333333 00000000 " +
                "00000000 00000000 3f70d315 3e40a8de 3e907ea6 41b535a5 00000000 00000000 00000000 3f800000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 453b565f 43870dd1 3f800000 3f800000 " +
                "3dd24116 3f36a388 3e81fc6a 43578906 00000000 00000000 3f800000 c4610000 00000000 42c80000 3e2bd387 " +
                "3e27e994 3fbbc86f 3f170a3d 3f33e59c 3f4219c5 3edd7a0e 3f170a3d",
            "rand0_height exponential+end-fade+beyond-end+pull+w+tint|42d08426 448a2b2b 3ace5abe 412f8710 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 3f800000 c235108b 3f06e33c 3f0a524e 3f11550d 3cbf3b58 " +
                "3b5ea12c 3f7276e5 3da6e16d 3e644abf bdecd3b2 3e9e6aaf 3f42bbd4 3f6c97a8 3f00f8bc 3e242e39 3ea92c64 " +
                "44bc6cef 3e96bf68 00000000 00000000 00000000 3f3710b3 bf32abde 3d1fbb7d 42be455d 3f71f5a1 bda4db2b " +
                "3e532807 3f3127b6 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 45608b4d " +
                "434117fb 3f800000 3f800000 3f71ae4a 3f7a5834 3cc824d6 43988a59 00000000 00000000 3f800000 450df5b9 " +
                "443ffdbd c34a4d10 3e1b213f 3e24eadd 3fbb6d52 3eb33333 3f6c3bb0 3ddbf90b 3e7fb33a 3eb33333",
            "rand1_power power-beyond|4283d79d 45b3d665 3b13445e 421cabf2 3f800000 44118905 3b532d49 3f800000 00000000 " +
                "00000000 3f800000 c0860ac9 3e3af20b 3c78c3e9 3e9bce91 3b4f4e6b 3afd62de 3f1e66f1 3ebd1eae 3ef2243e " +
                "3ef8fc54 3ea25b38 3eb690fd 3f62466a 3f14ba8f 3ed57c15 3f0a455e 449d590d 3eaa34c6 00000000 00000000 " +
                "00000000 3f214103 bf44bd29 bde60e47 42db49fa bf088763 3f5f281f 3f34b9a2 3f43938a 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 450b7c66 43d7d1c9 3f800000 3f800000 3f7c0b5f " +
                "3e93ca66 3e2f1090 4390adbb 00000000 00000000 3f800000 c50cb9ae 455e8ebb 42a6b4c6 3e22e378 3e24ec49 " +
                "3fbb6d7d 3eb851ec 3f1e66f1 3ebd1eae 3ef2243e 3eb851ec",
            "rand2_exp exponential+hf+pull+w|438a6277 4579410c 3aef6069 3dca4d4c 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 bed70cc4 3e89c46c 3f12ff1c 3f21c359 3c74d42c 3a825f60 3efeb892 3ef6ad59 " +
                "3f242c0d 3f5dfcc0 3e8f6c2a 3ee2ae44 3dad4fcf 3f4cc8ad 3e701ae1 3f543d70 44fe6938 3e826129 00000000 " +
                "00000000 00000000 bf30b435 3e89245b 3f2c1326 42d8adce 3c017792 be1727e6 3de1fd7c 3f644adf 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45678710 43cdd3c5 3f800000 3f800000 " +
                "3e94cb6f 3f575551 3f00c62f 4308fe4c 00000000 00000000 3f800000 437d0096 448e40a7 43487d5b 3e2aa492 " +
                "3e24eadd 3fbb6d52 3ebd70a4 3e94f697 3ef1eefe 3f515a02 3ebd70a4",
            "rand5_art exponential+end-fade+beyond-end+poly0.35+pull+w|431f734b 45057e2d 3b73d235 423d8486 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 3f800000 4247efc7 3da1ebb3 3edbd174 3ef3b4dc 3c5f94a0 " +
                "3b81c9e9 3f4f0329 3f6cb889 3f00015e 3f5794a2 3f161542 3ed6def5 3c488a3c 3db2c38d 3e3a3d4e 3f7cab43 " +
                "44c9dfc2 3e3c0e93 00000000 00000000 00000000 bece73c8 3f35bf67 3f13cfab 42e88ca3 3f130841 3e2d6025 " +
                "3f5eaa53 beed671b bf6b2083 bf3dfb3a 3f9b5fd1 3de17058 bfbf2fba bdd815f4 3eb0a1ef 3e31da96 459efe88 " +
                "42a23db6 3eb33333 3f800000 3d6d7489 3d78d275 3f79d36d 43be5fe2 00000000 00000000 3f800000 44219907 " +
                "c5365294 c3191eef 3e41e9c1 3e24eb5c 3fbb6d61 3ecccccd 3f4f0329 3f6cb889 3f00015e 3ecccccd",
            "rand11_height exponential+end-fade+hf+pull+w+tint|433acab6 4598c326 3b3f9574 41f03a30 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 3f800000 c126bb1f 3ec7ae9c 3f010ef1 3eefed25 3c89b462 3b60e77b " +
                "3f49e090 3f6480b9 3e655861 be33c741 3e9a21ab 3d027a93 3f4bdf19 3f1ecfea 3e92305d 3e43d8f7 45095d09 " +
                "3f560c0c 00000000 00000000 00000000 bec131fb 3f46c58b 3f013545 42a41404 3ec44f33 3efa69e5 bdb9d920 " +
                "be521e38 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 45793e9e 4431953a " +
                "3f800000 3f800000 3f7dca4d 3f6ac010 3f55df48 41ee0773 00000000 00000000 3f800000 44b69040 c3868d4a " +
                "41d0d7fb 3e3a27e5 3e24eadd 3fbb6d52 3eeb851f 3f4d81ef 3f2e7751 3f195b52 3eeb851f",
            "rand12_art exponential+poly0.80+hf+pull+w|438f444a 45800b3f 3a6a6dba 424ff28a 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 420f5014 3f3701bd 3f403513 3ee533a1 3cf3621b 3bb1dc95 3f176eaa " +
                "3f0bf03b 3d651679 3f149d9e 3d8c80bd 3f3e9cfa 3e78139b 3effb41f 3f6f9282 3f26fdb0 450148b2 3f7d5cd7 " +
                "00000000 00000000 00000000 bf650f98 bed13f0a 3e3838c0 42dd3429 3e6ac1fb bf79b627 3ee2d911 3f5961b0 " +
                "3fb6fc3c bf72b2e7 bf6f6bfb 3e2322d2 bf542237 bf80703d beefe347 3e14b86d 4583d024 436813bd 3f4ccccd " +
                "3f800000 3e9f4a12 3e3580cc 3f642928 43514360 00000000 00000000 3f800000 436e7772 443ea711 c38d066b " +
                "3e41e9e2 3e24ebaf 3fbb6d6b 3ef0a3d7 3ec5e5f5 3ee1153d 3f82c638 3ef0a3d7",
            "rand16_art exponential+end-fade+beyond-end+poly0.80+pull+w|43900d12 44d36400 3afabb92 420760ed 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 3f800000 c230d29e 3f47dfa9 3d7e8e7b 3f20f630 3c211382 " +
                "3b152171 3f1b5937 3e440839 3ee52033 3f3e310e 3f7ca29f 3e866e04 3ebb308d 3f319a8a 3e425121 3f595ff1 " +
                "44243f84 3f0e84f1 00000000 00000000 00000000 3f47b425 3f07c41a 3ea9f70b 4222ec5d bf7f708a bf7f7ac7 " +
                "bf7eff8c beafe77d bd62eedb 3f2af8a0 3f88f74d 3e3f8b84 be72dbeb bf0bfdfb be9d7b45 3d8018f3 452b2724 " +
                "444661da 3f4ccccd 3f800000 3f233ddb 3d85a128 3f374d50 42b9f87f 00000000 00000000 3f800000 c58e479f " +
                "c48c3320 428e7c37 3e34a982 3e3e4626 3fbe70b9 3f028f5c 3f233ddb 3d85a128 3f374d50 3f028f5c",
            "rand18_art exponential+poly0.00+hf+pull+w+tint|427da4df 455d09ac 3b643a83 41e765f8 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 3f800000 4244c0b8 3e209420 3e884847 3f09f85b 3c45a8c5 3b247699 " +
                "3dc66a4d 3f757b41 3f3a9604 bdf34e95 3f1bc4c6 3ed70f12 3e8f18a4 3f75f8b4 3f2f7b68 3f6def57 44dd2198 " +
                "3f5ad337 00000000 00000000 00000000 be63dc12 bf59d44c 3ef3a68d 411af51c bf43bb31 3ece6f7a 3d88f948 " +
                "3f6ca344 3ee5f63e bf114b2a beb274a6 3e3abdf6 3f2f7f0a bfbec083 bf7e5b8e 3ce9194b 45b4e8d9 43bf8613 " +
                "00000000 3f800000 3f11a033 3dc8eb80 3e6bc6b6 426d01b1 00000000 00000000 3f800000 441bbc5f c4002d60 " +
                "431579ab 3e3a27e5 3e24eadd 3fbb6d52 3f07ae14 3e4357c9 3e3e733a 3faab9c0 3f07ae14",
            "rand19_height exponential+pull+w+tint|41745af2 45ce40eb 3adb4f45 410a28cb 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 42068154 3ddf838c 3d184768 3f26e832 3c786a48 3bb2b126 3f4a69fe " +
                "3e53709f 3d390218 3eed6e97 3ef0d950 3e9a2dea 3f6ab7a7 3e55780a 3f24acd0 3d968eb4 45114390 3f72e10b " +
                "00000000 00000000 00000000 bf520140 be1ed064 3f0ceabb 429454f7 beb7f357 3ee554c2 3f4e9574 3f16fefc " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 44d26b33 43d607f7 3f800000 " +
                "3f800000 3e0f4a3d 3f0bddb0 3f73358c 43acfc60 00000000 00000000 3f800000 c4b0bb7f c3ef2cc9 c2db11c7 " +
                "3e57e35b 3e5c887f 3fc20945 3f0a3d71 3e7af14f 3dee30f6 3f38a810 3f0a3d71",
            "rand20_art exponential+end-fade+poly0.35+pull+w|4350f102 45a5f7e2 3b0b1889 40e30016 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 3f800000 c24217a6 3e2da044 3f538da6 3efe1a96 3ca435c5 3b242b92 " +
                "3ebce536 3e344015 3f798019 3eeea230 3e03b8ce 3f33e1cb 3f191106 3f1cff37 3f451877 3f1bbe90 450a7e41 " +
                "3f783d05 00000000 00000000 00000000 3f4a25b6 befe8ff8 3eb8162a 42433a6c 3e97e8d6 be80a785 be372277 " +
                "be8912b6 3f83f36f beaf0157 3edb1e90 3d4dd042 bf0dcbc8 bf995a56 3f653e80 3e3cae3b 4589e5c6 439d307d " +
                "3eb33333 3f800000 3eb437ba 3f58c6d3 3f4cbb87 43873ecb 00000000 00000000 3f800000 c5116f84 c5278b89 " +
                "42efb8a0 3e54f182 3e417368 3fbed15e 3f0ccccd 3e8a79c4 3ef2e687 3f986e99 3f0ccccd",
        };

        static readonly string[] M2ProgramCases =
        {
            "sf_d20 word0 before-start+tint|0|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 " +
                "3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 " +
                "00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 " +
                "3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 417093ed 415300cb 41c80000 3f25c282 " +
                "3e818315 3f3aec40 3ed70a3e 3f25c282 3e818315 3f3aec40 3ed70a3e",
            "sf_d20 word4 before-start+tint|4|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 " +
                "3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 " +
                "00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 " +
                "3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 417093ed 415300cb 41c80000 3ee8104f " +
                "3e35511d 3f02d893 3ed70a3e 3ee8104f 3e35511d 3f02d893 3ed70a3e",
            "pull_0_0_40 word0 before-start+pull|0|00000000 453b8000 3b449ba6 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c1b15579 3e6b2d8a 3e93fd0b 3d8fd157 3cc026ed 3b311760 3f58b9b4 " +
                "3ec5e51a 3f754244 3f3b6706 3a0eda1b 3e56c029 3f690795 3ef0a22c 3f7af8ce 3ecb7b36 43b7fe7d 3f2123f5 " +
                "00000000 00000000 00000000 3f689c15 be74653f 3eaf7327 42a00000 00000000 00000000 00000000 3f800000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 45997975 430a7e67 3f800000 " +
                "3f800000 3e7c4d1f 3dcef15e 3d7552c9 439f6780 00000000 00000000 3f800000 00000000 00000000 42200000 " +
                "3f25c282 3e818315 3f3aec40 3ed70a3e 3f25c282 3e818315 3f3aec40 3ed70a3e",
            "pull_0_0_40 word4 before-start+pull|4|00000000 453b8000 3b449ba6 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c1b15579 3e6b2d8a 3e93fd0b 3d8fd157 3cc026ed 3b311760 3f58b9b4 " +
                "3ec5e51a 3f754244 3f3b6706 3a0eda1b 3e56c029 3f690795 3ef0a22c 3f7af8ce 3ecb7b36 43b7fe7d 3f2123f5 " +
                "00000000 00000000 00000000 3f689c15 be74653f 3eaf7327 42a00000 00000000 00000000 00000000 3f800000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 45997975 430a7e67 3f800000 " +
                "3f800000 3e7c4d1f 3dcef15e 3d7552c9 439f6780 00000000 00000000 3f800000 00000000 00000000 42200000 " +
                "3ee8104f 3e35511d 3f02d893 3ed70a3e 3ee8104f 3e35511d 3f02d893 3ed70a3e",
            "height0 word0 exponential+hf+pull+w|0|42fc7410 45047c46 3abd4e14 426905e2 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c273b9ea 3e9bb8ea 3f628685 3e57c46c 3c82e529 3baa5cfc 3f244f57 " +
                "3dcd7b42 3f7d42e0 3d0d96de 3e843cf5 3f45cefe 3ea86cd8 3e97b7e1 3d9651fb 3db88f5a 44c08948 3e78d861 " +
                "00000000 00000000 00000000 bf6e8a4c 3eb9caf7 bc0c85dc 42904ee1 bea5075f 3d72df76 3f0ed526 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 457f5921 442ef93b 3f800000 " +
                "3f800000 3e3b3730 3e1dd5a6 3f688e75 43a38f76 00000000 00000000 3f800000 43650378 441013fc 42bf8a1d " +
                "3f25c282 3e818315 3f3aec40 3ed70a3e 3eb62eab 3e891acd 3eae920f 3ed70a3e",
            "height0 word4 exponential+hf+pull+w|4|42fc7410 45047c46 3abd4e14 426905e2 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c273b9ea 3e9bb8ea 3f628685 3e57c46c 3c82e529 3baa5cfc 3f244f57 " +
                "3dcd7b42 3f7d42e0 3d0d96de 3e843cf5 3f45cefe 3ea86cd8 3e97b7e1 3d9651fb 3db88f5a 44c08948 3e78d861 " +
                "00000000 00000000 00000000 bf6e8a4c 3eb9caf7 bc0c85dc 42904ee1 bea5075f 3d72df76 3f0ed526 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 457f5921 442ef93b 3f800000 " +
                "3f800000 3e3b3730 3e1dd5a6 3f688e75 43a38f76 00000000 00000000 3f800000 43650378 441013fc 42bf8a1d " +
                "3ee8104f 3e35511d 3f02d893 3ed70a3e 3dbc2063 3d12fcd9 3dd4252b 3dae5371",
            "art0 word0 exponential+end-fade+beyond-end+poly0.00+pull+w|0|430ef5b6 452cebb1 3a995d67 42144b76 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 3f800000 424c2143 3e04918b 3f477f79 3cbaa994 3bf3a12c " +
                "3ae55dc8 3f2fe176 3ea4e7f7 3eb5f00e 3ef6a704 3dd6ce36 3f3b1bda 3dfb75d6 3f02ae1a 3e804891 3e4a796e " +
                "44b179e6 3edfa11b 00000000 00000000 00000000 be17bddf 3f3495dd 3f317143 42345a77 be315a49 3d706a96 " +
                "bf2e3811 bf176a6a bf4d5180 3f38e99e 3f6e7868 3e38de4d bf0d68af bf0e16c1 3fa257af 3d32b42e 4587a757 " +
                "4404366d 00000000 3f800000 3f07951d 3f59ec0e 3f1c991a 43ab5a54 00000000 00000000 3f800000 45308141 " +
                "454584a9 c25085a3 3f25c282 3e818315 3f3aec40 3ed70a3e 3f07951d 3f59ec0e 3f1c991a 3ed70a3e",
            "art0 word4 exponential+end-fade+beyond-end+poly0.00+pull+w|4|430ef5b6 452cebb1 3a995d67 42144b76 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 3f800000 424c2143 3e04918b 3f477f79 3cbaa994 3bf3a12c " +
                "3ae55dc8 3f2fe176 3ea4e7f7 3eb5f00e 3ef6a704 3dd6ce36 3f3b1bda 3dfb75d6 3f02ae1a 3e804891 3e4a796e " +
                "44b179e6 3edfa11b 00000000 00000000 00000000 be17bddf 3f3495dd 3f317143 42345a77 be315a49 3d706a96 " +
                "bf2e3811 bf176a6a bf4d5180 3f38e99e 3f6e7868 3e38de4d bf0d68af bf0e16c1 3fa257af 3d32b42e 4587a757 " +
                "4404366d 00000000 3f800000 3f07951d 3f59ec0e 3f1c991a 43ab5a54 00000000 00000000 3f800000 45308141 " +
                "454584a9 c25085a3 3ee8104f 3e35511d 3f02d893 3ed70a3e 00000000 00000000 00000000 00000000",
            "power0_0.1 word0 power-inside|0|438034f6 45b64bdd 3aa707c6 405dac83 3f800000 44823948 3a82a5ae 3fd9999a " +
                "00000000 00000000 3f800000 c223d4e3 3e977d7a 3cb702db 3f5778db 3c62b400 3b7288cd 3e9c672d 3f0ba2f3 " +
                "3eab465b 3dcedda1 3dd7dee0 3ea0ab56 3cd4a9bb 3f274e4b 3dc79390 3f50f5e2 44925706 3f3652f1 00000000 " +
                "00000000 00000000 bf1975c0 bf43179c 3e7a99f2 426ab967 3f50c2ec bea5b73a bda7a61d be992625 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45a065eb 4395dcc7 3f800000 3f800000 " +
                "3e0bff27 3ef9e088 3f4e062c 41e7e636 00000000 00000000 3f800000 427a0794 42a6afb8 00000000 3f25c282 " +
                "3e818315 3f3aec40 3ed70a3e 3f1bfa84 3e696102 3f3e0578 3ed70a3e",
            "power0_0.1 word4 power-inside|4|438034f6 45b64bdd 3aa707c6 405dac83 3f800000 44823948 3a82a5ae 3fd9999a " +
                "00000000 00000000 3f800000 c223d4e3 3e977d7a 3cb702db 3f5778db 3c62b400 3b7288cd 3e9c672d 3f0ba2f3 " +
                "3eab465b 3dcedda1 3dd7dee0 3ea0ab56 3cd4a9bb 3f274e4b 3dc79390 3f50f5e2 44925706 3f3652f1 00000000 " +
                "00000000 00000000 bf1975c0 bf43179c 3e7a99f2 426ab967 3f50c2ec bea5b73a bda7a61d be992625 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45a065eb 4395dcc7 3f800000 3f800000 " +
                "3e0bff27 3ef9e088 3f4e062c 41e7e636 00000000 00000000 3f800000 427a0794 42a6afb8 00000000 3ee8104f " +
                "3e35511d 3f02d893 3ed70a3e 3eced8d0 3e219d48 3ee94175 3ebfac4e",
            "sun_a-0.2 word0 exponential+pull+tint|0|00000000 457a0000 3b03126f 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c19802d7 3f52c0fd 3f6d491c 3e0e6424 3bfe0a47 3aac35a6 3f30fa67 " +
                "3ef72a40 3f6bdbee be4ccccd 3f3a18a2 3f1cf3cc 3ea2a9f0 3f49ebe0 3e7aeebd 3f7c4cc9 44b1580f 3f333333 " +
                "00000000 00000000 00000000 3f70d315 3e40a8de 3e907ea6 41b535a5 00000000 00000000 00000000 3f800000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 453b565f 43870dd1 3f800000 " +
                "3f800000 3dd24116 3f36a388 3e81fc6a 43578906 00000000 00000000 3f800000 44610000 43340000 43870000 " +
                "3f25c282 3e818315 3f3aec40 3ed70a3e 3f41193f 3f2fa039 3eb0d34a 3ed70a3e",
            "sun_a-0.2 word4 exponential+pull+tint|4|00000000 457a0000 3b03126f 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c19802d7 3f52c0fd 3f6d491c 3e0e6424 3bfe0a47 3aac35a6 3f30fa67 " +
                "3ef72a40 3f6bdbee be4ccccd 3f3a18a2 3f1cf3cc 3ea2a9f0 3f49ebe0 3e7aeebd 3f7c4cc9 44b1580f 3f333333 " +
                "00000000 00000000 00000000 3f70d315 3e40a8de 3e907ea6 41b535a5 00000000 00000000 00000000 3f800000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 453b565f 43870dd1 3f800000 " +
                "3f800000 3dd24116 3f36a388 3e81fc6a 43578906 00000000 00000000 3f800000 44610000 43340000 43870000 " +
                "3ee8104f 3e35511d 3f02d893 3ed70a3e 3d8aae0d 3cd8b52e 3d9c62b4 3d8081b2",
            "rand0_height word0 exponential+end-fade+beyond-end+pull+w+tint|0|42d08426 448a2b2b 3ace5abe 412f8710 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 3f800000 c235108b 3f06e33c 3f0a524e 3f11550d " +
                "3cbf3b58 3b5ea12c 3f7276e5 3da6e16d 3e644abf bdecd3b2 3e9e6aaf 3f42bbd4 3f6c97a8 3f00f8bc 3e242e39 " +
                "3ea92c64 44bc6cef 3e96bf68 00000000 00000000 00000000 3f3710b3 bf32abde 3d1fbb7d 42be455d 3f71f5a1 " +
                "bda4db2b 3e532807 3f3127b6 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 " +
                "45608b4d 434117fb 3f800000 3f800000 3f71ae4a 3f7a5834 3cc824d6 43988a59 00000000 00000000 3f800000 " +
                "450df5b9 443ffdbd c34a4d10 3f25c282 3e818315 3f3aec40 3ed70a3e 3f6c3bb0 3ddbf90b 3e7fb33a 3ed70a3e",
            "rand0_height word4 exponential+end-fade+beyond-end+pull+w+tint|4|42d08426 448a2b2b 3ace5abe 412f8710 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 3f800000 c235108b 3f06e33c 3f0a524e 3f11550d " +
                "3cbf3b58 3b5ea12c 3f7276e5 3da6e16d 3e644abf bdecd3b2 3e9e6aaf 3f42bbd4 3f6c97a8 3f00f8bc 3e242e39 " +
                "3ea92c64 44bc6cef 3e96bf68 00000000 00000000 00000000 3f3710b3 bf32abde 3d1fbb7d 42be455d 3f71f5a1 " +
                "bda4db2b 3e532807 3f3127b6 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 " +
                "45608b4d 434117fb 3f800000 3f800000 3f71ae4a 3f7a5834 3cc824d6 43988a59 00000000 00000000 3f800000 " +
                "450df5b9 443ffdbd c34a4d10 3ee8104f 3e35511d 3f02d893 3ed70a3e 00000000 00000000 00000000 00000000",
            "sf_d300 word0 before-start|0|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 " +
                "3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 " +
                "00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 " +
                "3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 43221738 437c70f9 41c80000 3f25c282 " +
                "3e818315 3f3aec40 3ed70a3e 3f25c282 3e818315 3f3aec40 3ed70a3e",
            "sf_d300 word4 before-start|4|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 " +
                "3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 " +
                "00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 " +
                "3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 43221738 437c70f9 41c80000 3ee8104f " +
                "3e35511d 3f02d893 3ed70a3e 3ee8104f 3e35511d 3f02d893 3ed70a3e",
            "pull_0_0_300 word0 exponential+pull|0|00000000 453b8000 3b449ba6 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c1b15579 3e6b2d8a 3e93fd0b 3d8fd157 3cc026ed 3b311760 3f58b9b4 " +
                "3ec5e51a 3f754244 3f3b6706 3a0eda1b 3e56c029 3f690795 3ef0a22c 3f7af8ce 3ecb7b36 43b7fe7d 3f2123f5 " +
                "00000000 00000000 00000000 3f689c15 be74653f 3eaf7327 42a00000 00000000 00000000 00000000 3f800000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 45997975 430a7e67 3f800000 " +
                "3f800000 3e7c4d1f 3dcef15e 3d7552c9 439f6780 00000000 00000000 3f800000 00000000 00000000 43960000 " +
                "3f25c282 3e818315 3f3aec40 3ed70a3e 3ee428c6 3e8a705b 3ed2980f 3ed70a3e",
            "pull_0_0_300 word4 exponential+pull|4|00000000 453b8000 3b449ba6 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c1b15579 3e6b2d8a 3e93fd0b 3d8fd157 3cc026ed 3b311760 3f58b9b4 " +
                "3ec5e51a 3f754244 3f3b6706 3a0eda1b 3e56c029 3f690795 3ef0a22c 3f7af8ce 3ecb7b36 43b7fe7d 3f2123f5 " +
                "00000000 00000000 00000000 3f689c15 be74653f 3eaf7327 42a00000 00000000 00000000 00000000 3f800000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 45997975 430a7e67 3f800000 " +
                "3f800000 3e7c4d1f 3dcef15e 3d7552c9 439f6780 00000000 00000000 3f800000 00000000 00000000 43960000 " +
                "3ee8104f 3e35511d 3f02d893 3ed70a3e 3e6fe286 3dbb6d7e 3e87417f 3e5e4994",
            "height0 word0 exponential+end-fade+beyond-end+hf+pull+w+tint|0|42fc7410 45047c46 3abd4e14 426905e2 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 3f800000 c273b9ea 3e9bb8ea 3f628685 3e57c46c " +
                "3c82e529 3baa5cfc 3f244f57 3dcd7b42 3f7d42e0 3d0d96de 3e843cf5 3f45cefe 3ea86cd8 3e97b7e1 3d9651fb " +
                "3db88f5a 44c08948 3e78d861 00000000 00000000 00000000 bf6e8a4c 3eb9caf7 bc0c85dc 42904ee1 bea5075f " +
                "3d72df76 3f0ed526 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 " +
                "457f5921 442ef93b 3f800000 3f800000 3e3b3730 3e1dd5a6 3f688e75 43a38f76 00000000 00000000 3f800000 " +
                "c4db6aa5 c4a6fbb1 c1fb563e 3f25c282 3e818315 3f3aec40 3ed70a3e 3ea1f527 3e21e155 3f69d681 3ed70a3e",
            "height0 word4 exponential+end-fade+beyond-end+hf+pull+w+tint|4|42fc7410 45047c46 3abd4e14 426905e2 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 3f800000 c273b9ea 3e9bb8ea 3f628685 3e57c46c " +
                "3c82e529 3baa5cfc 3f244f57 3dcd7b42 3f7d42e0 3d0d96de 3e843cf5 3f45cefe 3ea86cd8 3e97b7e1 3d9651fb " +
                "3db88f5a 44c08948 3e78d861 00000000 00000000 00000000 bf6e8a4c 3eb9caf7 bc0c85dc 42904ee1 bea5075f " +
                "3d72df76 3f0ed526 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 " +
                "457f5921 442ef93b 3f800000 3f800000 3e3b3730 3e1dd5a6 3f688e75 43a38f76 00000000 00000000 3f800000 " +
                "c4db6aa5 c4a6fbb1 c1fb563e 3ee8104f 3e35511d 3f02d893 3ed70a3e 00000000 00000000 00000000 00000000",
            "art0 word0 exponential+end-fade+beyond-end+poly0.00+pull+w+tint|0|430ef5b6 452cebb1 3a995d67 42144b76 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 3f800000 424c2143 3e04918b 3f477f79 3cbaa994 " +
                "3bf3a12c 3ae55dc8 3f2fe176 3ea4e7f7 3eb5f00e 3ef6a704 3dd6ce36 3f3b1bda 3dfb75d6 3f02ae1a 3e804891 " +
                "3e4a796e 44b179e6 3edfa11b 00000000 00000000 00000000 be17bddf 3f3495dd 3f317143 42345a77 be315a49 " +
                "3d706a96 bf2e3811 bf176a6a bf4d5180 3f38e99e 3f6e7868 3e38de4d bf0d68af bf0e16c1 3fa257af 3d32b42e " +
                "4587a757 4404366d 00000000 3f800000 3f07951d 3f59ec0e 3f1c991a 43ab5a54 00000000 00000000 3f800000 " +
                "c34ef046 45587aa1 c2a13193 3f25c282 3e818315 3f3aec40 3ed70a3e 3f043188 3f58f631 3f18b265 3ed70a3e",
            "art0 word4 exponential+end-fade+beyond-end+poly0.00+pull+w+tint|4|430ef5b6 452cebb1 3a995d67 42144b76 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 3f800000 424c2143 3e04918b 3f477f79 3cbaa994 " +
                "3bf3a12c 3ae55dc8 3f2fe176 3ea4e7f7 3eb5f00e 3ef6a704 3dd6ce36 3f3b1bda 3dfb75d6 3f02ae1a 3e804891 " +
                "3e4a796e 44b179e6 3edfa11b 00000000 00000000 00000000 be17bddf 3f3495dd 3f317143 42345a77 be315a49 " +
                "3d706a96 bf2e3811 bf176a6a bf4d5180 3f38e99e 3f6e7868 3e38de4d bf0d68af bf0e16c1 3fa257af 3d32b42e " +
                "4587a757 4404366d 00000000 3f800000 3f07951d 3f59ec0e 3f1c991a 43ab5a54 00000000 00000000 3f800000 " +
                "c34ef046 45587aa1 c2a13193 3ee8104f 3e35511d 3f02d893 3ed70a3e 00000000 00000000 00000000 00000000",
            "power0_1.2 word0 power-beyond|0|438034f6 45b64bdd 3aa707c6 405dac83 3f800000 44823948 3a82a5ae 3fd9999a " +
                "00000000 00000000 3f800000 c223d4e3 3e977d7a 3cb702db 3f5778db 3c62b400 3b7288cd 3e9c672d 3f0ba2f3 " +
                "3eab465b 3dcedda1 3dd7dee0 3ea0ab56 3cd4a9bb 3f274e4b 3dc79390 3f50f5e2 44925706 3f3652f1 00000000 " +
                "00000000 00000000 bf1975c0 bf43179c 3e7a99f2 426ab967 3f50c2ec bea5b73a bda7a61d be992625 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45a065eb 4395dcc7 3f800000 3f800000 " +
                "3e0bff27 3ef9e088 3f4e062c 41e7e636 00000000 00000000 3f800000 443b85af 447a0794 00000000 3f25c282 " +
                "3e818315 3f3aec40 3ed70a3e 3e9c672d 3f0ba2f3 3eab465a 3ed70a3e",
            "power0_1.2 word4 power-beyond|4|438034f6 45b64bdd 3aa707c6 405dac83 3f800000 44823948 3a82a5ae 3fd9999a " +
                "00000000 00000000 3f800000 c223d4e3 3e977d7a 3cb702db 3f5778db 3c62b400 3b7288cd 3e9c672d 3f0ba2f3 " +
                "3eab465b 3dcedda1 3dd7dee0 3ea0ab56 3cd4a9bb 3f274e4b 3dc79390 3f50f5e2 44925706 3f3652f1 00000000 " +
                "00000000 00000000 bf1975c0 bf43179c 3e7a99f2 426ab967 3f50c2ec bea5b73a bda7a61d be992625 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45a065eb 4395dcc7 3f800000 3f800000 " +
                "3e0bff27 3ef9e088 3f4e062c 41e7e636 00000000 00000000 3f800000 443b85af 447a0794 00000000 3ee8104f " +
                "3e35511d 3f02d893 3ed70a3e 00000000 00000000 00000000 00000000",
            "sun_a-0.2 word0 exponential+tint|0|00000000 457a0000 3b03126f 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c19802d7 3f52c0fd 3f6d491c 3e0e6424 3bfe0a47 3aac35a6 3f30fa67 " +
                "3ef72a40 3f6bdbee be4ccccd 3f3a18a2 3f1cf3cc 3ea2a9f0 3f49ebe0 3e7aeebd 3f7c4cc9 44b1580f 3f333333 " +
                "00000000 00000000 00000000 3f70d315 3e40a8de 3e907ea6 41b535a5 00000000 00000000 00000000 3f800000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 453b565f 43870dd1 3f800000 " +
                "3f800000 3dd24116 3f36a388 3e81fc6a 43578906 00000000 00000000 3f800000 44610000 c3960000 00000000 " +
                "3f25c282 3e818315 3f3aec40 3ed70a3e 3f4128a6 3f2fd0dd 3eb00715 3ed70a3e",
            "sun_a-0.2 word4 exponential+tint|4|00000000 457a0000 3b03126f 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 00000000 3f800000 c19802d7 3f52c0fd 3f6d491c 3e0e6424 3bfe0a47 3aac35a6 3f30fa67 " +
                "3ef72a40 3f6bdbee be4ccccd 3f3a18a2 3f1cf3cc 3ea2a9f0 3f49ebe0 3e7aeebd 3f7c4cc9 44b1580f 3f333333 " +
                "00000000 00000000 00000000 3f70d315 3e40a8de 3e907ea6 41b535a5 00000000 00000000 00000000 3f800000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 453b565f 43870dd1 3f800000 " +
                "3f800000 3dd24116 3f36a388 3e81fc6a 43578906 00000000 00000000 3f800000 44610000 c3960000 00000000 " +
                "3ee8104f 3e35511d 3f02d893 3ed70a3e 3d8b3433 3cd986ce 3d9cf9fb 3d80fe01",
            "rand3_height word0 exponential+end-fade+pull+w|0|4372e2d6 45b43f5a 3a9260f8 424388b6 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 3f800000 c1bcd54a 3e79f251 3f6b671b 3cbb4690 3c85c81b 3b1c7485 " +
                "3f4ab097 3f1c4abe 3f693a61 3f53646f 3f24ef6c 3d8418d4 3f2ace4c 3de002dd 3f7b7060 3f5f10f5 44649cc2 " +
                "3e3e626f 00000000 00000000 00000000 3ee0c78b 3f34932d 3f0e77eb 42a69e28 3f0426f1 3f7854f1 3e7bac2c " +
                "bf3fdbab 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 459511a2 42bbd9a2 " +
                "3f800000 3f800000 3f5e2371 3f44c5c2 3f490d53 43aeb7e9 00000000 00000000 3f800000 45482756 c4949c29 " +
                "42a6f24e 3f25c282 3e818315 3f3aec40 3ed70a3e 3f5e1b1f 3f44b246 3f490b3d 3ed70a3e",
            "rand3_height word4 exponential+end-fade+pull+w|4|4372e2d6 45b43f5a 3a9260f8 424388b6 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 3f800000 c1bcd54a 3e79f251 3f6b671b 3cbb4690 3c85c81b 3b1c7485 " +
                "3f4ab097 3f1c4abe 3f693a61 3f53646f 3f24ef6c 3d8418d4 3f2ace4c 3de002dd 3f7b7060 3f5f10f5 44649cc2 " +
                "3e3e626f 00000000 00000000 00000000 3ee0c78b 3f34932d 3f0e77eb 42a69e28 3f0426f1 3f7854f1 3e7bac2c " +
                "bf3fdbab 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 459511a2 42bbd9a2 " +
                "3f800000 3f800000 3f5e2371 3f44c5c2 3f490d53 43aeb7e9 00000000 00000000 3f800000 45482756 c4949c29 " +
                "42a6f24e 3ee8104f 3e35511d 3f02d893 3ed70a3e 3988fea1 38d61304 399a7c34 397de3d8",
            "sf_d451 word0 exponential|0|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 " +
                "3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 " +
                "00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 " +
                "3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 4337d0eb 43cdeb97 41c80000 3f25c282 " +
                "3e818315 3f3aec40 3ed70a3e 3f25b885 3e816c63 3f3ac7fc 3ed70a3e",
            "sf_d451 word4 exponential|4|43e10000 45dac000 3ac49ba6 00000000 00000000 00000000 00000000 00000000 " +
                "00000000 00000000 3f800000 c1f00000 3f165eda 3e3d179a 3f030c72 3b23d70a 3a03126f 3dc0c3cc 3e9b5769 " +
                "3db9b17a 00000000 3f31852f 3d2b8ab7 3f7b7107 3f76fa5e 3f276778 3f1d9584 44610000 3e99999a 00000000 " +
                "00000000 00000000 3f11de9a bf2f0b1f 3ee9642a 00000000 00000000 ba83126f 3f808312 3b449ba6 00000000 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 45603b48 43be32fd 3f800000 3f800000 " +
                "3f57a94e 3f04e551 3f23ea28 45034000 00000000 00000000 3f800000 4337d0eb 43cdeb97 41c80000 3ee8104f " +
                "3e35511d 3f02d893 3ed70a3e 3ee779b0 3e34db6e 3f0283a6 3ed67eac",
            "height1 word0 exponential+end-fade+hf+pull+w|0|42b00fab 44ebf5fc 3b45fff1 40840cd7 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 3f800000 c260a31a 3f0f32a2 3f5a36e1 3f1d42f6 3c7f13d4 3bb5b6de " +
                "3e50dfd7 3c87c7dc 3e89d3c9 3e949f11 3d77a044 3e347bdf 3ebcd170 3f1279b2 3e06bc85 3eb96b17 450c929a " +
                "3f7b019e 00000000 00000000 00000000 bf0b50c2 3f3993f6 3ed838e7 429da9e8 becad586 beec2ffa 3f785b70 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 44c590c4 44372a33 " +
                "3f800000 3f800000 3f3372c5 3f767829 3cae27ba 437e794c 00000000 00000000 3f800000 c3136873 c4959ec9 " +
                "c290e05c 3f25c282 3e818315 3f3aec40 3ed70a3e 3f1dc6c8 3f044f1b 3e8629d0 3ed70a3e",
            "height1 word4 exponential+end-fade+hf+pull+w|4|42b00fab 44ebf5fc 3b45fff1 40840cd7 00000000 00000000 " +
                "00000000 00000000 00000000 00000000 3f800000 c260a31a 3f0f32a2 3f5a36e1 3f1d42f6 3c7f13d4 3bb5b6de " +
                "3e50dfd7 3c87c7dc 3e89d3c9 3e949f11 3d77a044 3e347bdf 3ebcd170 3f1279b2 3e06bc85 3eb96b17 450c929a " +
                "3f7b019e 00000000 00000000 00000000 bf0b50c2 3f3993f6 3ed838e7 429da9e8 becad586 beec2ffa 3f785b70 " +
                "00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 44c590c4 44372a33 " +
                "3f800000 3f800000 3f3372c5 3f767829 3cae27ba 437e794c 00000000 00000000 3f800000 c3136873 c4959ec9 " +
                "c290e05c 3ee8104f 3e35511d 3f02d893 3ed70a3e 3b2a6772 3a852407 3b4028fb 3b1de752",
        };

        // The rig dump of Retail map 1 (12.1.5.70448; map flags [159399517, 134479888, 140]): LightParams 5985 (Light 204), 5944 (Light 193), 5951 (Light 208),
        // 6033 (Light 239), every key; the colours 0xRRGGBB and the floats (float32, widened) in WorldLightColumns' orders.
        static readonly FogDay Day5985 = new FogDay(
            new[] { 0, 480, 720, 1440, 2340, 2520, 2640 },
            new[]
            {
                0x24308e, 0x3c5976, 0x000000, 0x232250, 0x4c466e, 0x8c80a5, 0x59456b, 0x221b2f,
                0xffffff, 0x699ccc, 0x000000, 0x12001d, 0x000000, 0x334c51, 0x1d3f69, 0x54442d,
                0x0c1b25, 0x000000, 0x234b73, 0x2a5076, 0x3d355e, 0xe5a678, 0x191522, 0x161a30,
                0x24308e, 0x3c5976, 0x000000, 0x232250, 0x4c466e, 0x8c80a5, 0x59456b, 0x221b2f,
                0xfffbec, 0xa499cc, 0x000000, 0x12001d, 0x000000, 0x334c51, 0x1d3f69, 0x54442d,
                0x0c1b25, 0x000000, 0x234b73, 0x2a5076, 0x3d355e, 0xe5a678, 0x191522, 0x161a30,
                0x785400, 0x989b9b, 0x466975, 0xedac68, 0xe59449, 0xc2774a, 0x9c6751, 0x564649,
                0xff9e00, 0xff7900, 0x000000, 0x8a1f63, 0x000000, 0x596490, 0x364456, 0x4f422a,
                0x545454, 0x000000, 0x717171, 0x6f6f6f, 0x785960, 0xe5a678, 0x684238, 0x6e5347,
                0x5e3200, 0xb7a5b8, 0xa06394, 0xffe2af, 0xf6b258, 0xc1634e, 0xb3604d, 0x784b3a,
                0xfff7de, 0x9c6108, 0x000000, 0x310035, 0x000000, 0x525a5e, 0x373d45, 0x403e23,
                0x3e362f, 0x000000, 0x9c8a9d, 0x9e7865, 0x955b4e, 0xe5a678, 0x622f38, 0x975649,
                0x7b3f00, 0x7f6575, 0x744e72, 0xcc947f, 0xfa9c5f, 0xf95b3a, 0xdd5844, 0x7d393e,
                0xff8426, 0xed4a01, 0x000000, 0x4a1963, 0x000000, 0x677ac2, 0x495eb2, 0x4c4129,
                0x464956, 0x000000, 0x7f6575, 0x7f6575, 0xbb4f48, 0xe5a678, 0x622f38, 0x9f5344,
                0x3f3021, 0x694f5e, 0x120c12, 0x583127, 0x985129, 0xf7431f, 0xaf3b3e, 0x4c2124,
                0xff6d00, 0xff4600, 0x000000, 0x501f6d, 0x000000, 0x60698d, 0x4e5984, 0x4e422a,
                0x484e5f, 0x000000, 0x694f5e, 0x694f5e, 0x713638, 0xe5a678, 0x32161b, 0x461c1f,
                0x4b3453, 0x416291, 0x000000, 0x2b2a62, 0x4c466e, 0x8c80a5, 0x826c97, 0x241d32,
                0xffffff, 0x699ccc, 0x000000, 0x12001d, 0x000000, 0x334c51, 0x364658, 0x50422b,
                0x323b42, 0x000000, 0x31517e, 0x355583, 0x3d355e, 0xe5a678, 0x191522, 0x1a1e35,
            },
            new[]
            {
                0.0, 0.15000000596046448, 3.0, 30.0, 0.0033333334140479565, 1.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                0.005018711090087891, 0.3273468017578125, 0.382965087890625, 0.285400390625, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.15000000596046448, 3.0, 30.0, 0.0033333334140479565, 1.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                0.005018711090087891, 0.3273468017578125, 0.382965087890625, 0.285400390625, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.15000000596046448, 2.5, 30.0, 0.0024999999441206455, 1.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                0.0004410743713378906, 0.3416290283203125, 0.45489501953125, 0.2091522216796875, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.15000000596046448, 3.0, 30.0, 0.0024999999441206455, 1.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                0.002940654754638672, 1.00372314453125, -0.000732421875, 9.1552734375e-05, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.15000000596046448, 2.0, 30.0, 0.0024999999441206455, 1.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 900.0, 2200.0, 0.30000001192092896,
                0.002940654754638672, 1.00372314453125, -0.000732421875, 9.1552734375e-05, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.15000000596046448, 3.0, 30.0, 0.004000000189989805, 2.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                0.002830982208251953, 0.1135101318359375, 0.672027587890625, 0.20953369140625, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.15000000596046448, 3.0, 30.0, 0.0033333334140479565, 1.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                0.007778167724609375, 0.167236328125, 0.850189208984375, -0.023040771484375, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
            }, 0.0, 0);

        static readonly FogDay Day5944 = new FogDay(
            new[] { 0, 360, 720, 1440, 2160, 2280, 2520, 2640 },
            new[]
            {
                0x002059, 0x443d4f, 0x000000, 0x0c2741, 0x0e2d4a, 0x0f304e, 0x0f3456, 0x0e2a45,
                0xe8f1ff, 0x326184, 0x000000, 0x123851, 0x000000, 0x213e2f, 0x000c18, 0x585445,
                0x2b4351, 0x000000, 0x2d3b59, 0x000000, 0x113a61, 0xdab68e, 0x0c1a25, 0x0a1e32,
                0x002059, 0x484154, 0x000000, 0x001d3a, 0x00254a, 0x103254, 0x113459, 0x0e2a45,
                0xe8f1ff, 0x3a5f80, 0x000000, 0x134351, 0x000000, 0x213e2f, 0x000c18, 0x585445,
                0x2b4351, 0x000000, 0x2d3b59, 0x000000, 0x133b64, 0xdfb98a, 0x000c17, 0x051b33,
                0xff6700, 0x5d7d9c, 0x234a54, 0x448c80, 0xd27948, 0xffab40, 0xe4b74c, 0x5d7d6d,
                0xffd296, 0xffb33c, 0x000000, 0x44a2ab, 0x000000, 0x5b4842, 0x14302f, 0x545845,
                0x2b5143, 0x000000, 0x404b66, 0x000000, 0x9e8d65, 0xe4bd87, 0x3f5f4f, 0x46776d,
                0x784000, 0x7f95aa, 0x001f49, 0x3aa2cf, 0x99dcf5, 0x96c4ca, 0x9cb0b2, 0x4d788f,
                0xfff7de, 0xffc78a, 0x000000, 0x2b6984, 0x000000, 0x114b59, 0x001d29, 0x585445,
                0x234757, 0x000000, 0x676e82, 0x000000, 0x739fb5, 0xefc580, 0x315f77, 0x337ca6,
                0xca5900, 0x7e7d99, 0x000a18, 0x3c5976, 0x959895, 0xb79867, 0xb58865, 0x4e5366,
                0xffe9ba, 0xffcf98, 0x000000, 0x6a615c, 0x000000, 0x41494c, 0x0d2030, 0x585445,
                0x284453, 0x000000, 0x4d4a66, 0x000000, 0x846d69, 0xd0af95, 0x2b3345, 0x31445a,
                0xd85e00, 0x7e7997, 0x000710, 0x3d4d68, 0x958d85, 0xbc9156, 0xba8159, 0x4f4d5f,
                0xffe6b5, 0xffd19b, 0x000000, 0x745f55, 0x000000, 0x49484a, 0x102032, 0x585445,
                0x294352, 0x000000, 0x484462, 0x000000, 0x87655d, 0xd1b093, 0x2a2b3c, 0x303b4e,
                0x1c1007, 0x484154, 0x000000, 0x3e354a, 0x947765, 0xc88335, 0xc3743f, 0x504152,
                0xffe2a9, 0xffd4a0, 0x000000, 0x8a5d48, 0x000000, 0x5a4846, 0x152235, 0x585445,
                0x2b4351, 0x000000, 0x403859, 0x000000, 0x8e5544, 0xd5b291, 0x291d2c, 0x302835,
                0x002059, 0x484154, 0x000000, 0x0b2945, 0x0d2f4f, 0x123b61, 0x173f64, 0x0e2a45,
                0xe8f1ff, 0x326184, 0x000000, 0x123851, 0x000000, 0x213e2f, 0x000c18, 0x3b3728,
                0x1d313d, 0x000000, 0x2d3b59, 0x000000, 0x17446f, 0xd6b390, 0x041320, 0x081e33,
            },
            new[]
            {
                0.0, 0.20000000298023224, 4.0, 0.0, 0.0012499999720603228, 2.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 1000.0, 2000.0, 0.4000000059604645,
                -0.005764484405517578, 0.15069580078125, 1.188140869140625, -0.335968017578125, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.15000000596046448, 4.0, 0.0, 0.0012499999720603228, 2.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 1000.0, 2000.0, 0.4000000059604645,
                -0.005764484405517578, 0.15069580078125, 1.188140869140625, -0.335968017578125, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 5.0, 0.0, 0.0012499999720603228, 2.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 1000.0, 2000.0, 0.4000000059604645,
                0.002940654754638672, 1.00372314453125, -0.000732421875, 9.1552734375e-05, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.20000000298023224, 3.0, 0.0, 0.0016666667070239782, 2.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 1000.0, 2000.0, 0.4000000059604645,
                -0.003719806671142578, 0.120574951171875, 0.609619140625, 0.285003662109375, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.20000000298023224, 5.0, 0.0, 0.0016666667070239782, 2.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 1000.0, 2000.0, 0.4000000059604645,
                -0.002323627471923828, 0.10809326171875, 0.75732421875, 0.142913818359375, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.20000000298023224, 5.333333492279053, 0.0, 0.0016666667070239782, 2.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 1000.0, 2000.0, 0.4000000059604645,
                -0.002090930938720703, 0.1060129776597023, 0.7819417715072632, 0.119232177734375, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.20000000298023224, 6.0, 0.0, 0.0016666667070239782, 2.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 1000.0, 2000.0, 0.4000000059604645,
                -0.0016255378723144531, 0.1018524169921875, 0.8311767578125, 0.071868896484375, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.20000000298023224, 4.0, 0.0, 0.0012499999720603228, 2.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 1000.0, 2000.0, 0.4000000059604645,
                -0.005764484405517578, 0.15069580078125, 1.188140869140625, -0.335968017578125, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
            }, 0.10000000149011612, 0);

        static readonly FogDay Day5951 = new FogDay(
            new[] { 0, 1440, 2870 },
            new[]
            {
                0x001d2a, 0x6d85c0, 0x113247, 0x0e9280, 0x0e808c, 0x2b5f7f, 0x27587e, 0x16467f,
                0xe0f7ff, 0x007983, 0x000000, 0x2d6773, 0x000000, 0x7a8092, 0x494e58, 0x717992,
                0x139d9f, 0x000000, 0x596078, 0x4c516c, 0x23537a, 0x669abb, 0x002745, 0x073e58,
                0x0f6d62, 0x556fad, 0x05495c, 0x5a947b, 0x026e85, 0x21698c, 0x1c698e, 0x2b4e7d,
                0xe0f7ff, 0x578ea4, 0x000000, 0x6bb9bc, 0x000000, 0x5d7c78, 0x455561, 0x5f6578,
                0x528094, 0x000000, 0x556fad, 0x60517b, 0x126790, 0x669abb, 0x002333, 0x17526f,
                0x001d2a, 0x6c84bf, 0x103247, 0x0e927f, 0x0d7f8b, 0x2a5f7f, 0x26587e, 0x16467e,
                0xe0f7ff, 0x017983, 0x000000, 0x2d6773, 0x000000, 0x797f91, 0x484e58, 0x707891,
                0x139c9e, 0x000000, 0x586078, 0x4c516c, 0x22537a, 0x669abb, 0x002644, 0x073e58,
            },
            new[]
            {
                0.0, 0.0, 6.0, 125.0, 0.0033333334140479565, 3.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 700.0, 400.0, 0.30000001192092896,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.004999999888241291, 3.5, 125.0, 0.0033333334140479565, 3.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 700.0, 400.0, 0.30000001192092896,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 3.472203388810158e-05, 5.982638835906982, 125.0, 0.0033333334140479565, 3.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 700.0, 400.0, 0.30000001192092896,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
            }, 0.5, 0);

        static readonly FogDay Day6033 = new FogDay(
            new[] { 0, 360, 480, 720, 780, 1440, 1450, 2340, 2520, 2640 },
            new[]
            {
                0x09114a, 0x5b6a84, 0x000000, 0x2d2d47, 0x3d2e4d, 0x463458, 0x413051, 0x211819,
                0xffffff, 0x303542, 0x000000, 0x140020, 0x000000, 0x39453c, 0x352b4a, 0x5a3b34,
                0x3a3a33, 0x000000, 0x46505f, 0x4a5261, 0x34273e, 0xd3b89a, 0x1d1821, 0x1e1d26,
                0x09114a, 0x5b6a84, 0x000000, 0x2d2d47, 0x3d2e4d, 0x463458, 0x413051, 0x211819,
                0xfff6d0, 0x303741, 0x000000, 0x1f042c, 0x000000, 0x555045, 0x352b4a, 0x5a3b34,
                0x3a3a33, 0x000000, 0x46505f, 0x4a5261, 0x34273e, 0xd3b89a, 0x1d1821, 0x1e1d26,
                0x09114a, 0x5b6a84, 0x000000, 0x2d2d47, 0x3d2e4d, 0x463458, 0x413051, 0x211819,
                0xffd88a, 0x313941, 0x000000, 0x230531, 0x000000, 0x5e5448, 0x3f2f51, 0x5a3b34,
                0x3a3a33, 0x000000, 0x46505f, 0x4a5261, 0x34273e, 0xd3b89a, 0x1d1821, 0x1e1d26,
                0x2d2122, 0x585f6d, 0x20141f, 0x584c58, 0x6b4b50, 0x814e54, 0x784950, 0x362322,
                0xff9e00, 0x524b3f, 0x000000, 0x2a0839, 0x000000, 0x725c4f, 0x563861, 0x5a3b34,
                0x3a3a33, 0x000000, 0x57606e, 0x5b5b64, 0x6b4147, 0xd3b89a, 0x483c34, 0x574446,
                0x372618, 0x5d6472, 0x281a26, 0x63545d, 0x775251, 0x865054, 0x7c4b50, 0x3b2625,
                0xffa512, 0x5b503e, 0x000000, 0x2c093b, 0x000000, 0x725c4f, 0x563861, 0x5a3b34,
                0x3a3a33, 0x000000, 0x5c6472, 0x605d65, 0x6f4347, 0xd3b89a, 0x4a3b34, 0x5b4446,
                0xa47856, 0xa0a4ad, 0x83557e, 0xddae8f, 0xf9a45d, 0xbe6a5e, 0xb46357, 0x73514f,
                0xfff7de, 0xb78439, 0x000000, 0x411053, 0x000000, 0x725c4f, 0x563861, 0x5a3b34,
                0x3a3a33, 0x000000, 0x8e939e, 0x92776e, 0xa45c51, 0xd3b89a, 0x6c3937, 0x91504d,
                0xa37755, 0x9fa3ac, 0x82547d, 0xdcad8e, 0xf8a35c, 0xbd695d, 0xb36256, 0x72504e,
                0xfff5dc, 0xba8639, 0x000000, 0x421154, 0x000000, 0x725c4f, 0x563861, 0x5a3b34,
                0x3a3a33, 0x000000, 0x8d929d, 0x91766e, 0xa35b51, 0xd3b89a, 0x6b3836, 0x904f4c,
                0x4b3524, 0x737c8e, 0x31202f, 0x8e6758, 0xb97758, 0x7e4c58, 0x794b56, 0x583837,
                0xff8426, 0x64533e, 0x000000, 0x250633, 0x000000, 0x725c4f, 0x563861, 0x5a3b34,
                0x3a3a33, 0x000000, 0x6f7787, 0x6b717b, 0x724b5c, 0xd3b89a, 0x4d2e2d, 0x673f41,
                0x221c17, 0x525967, 0x0b070a, 0x41302b, 0x523429, 0x633f5d, 0x603e5f, 0x312222,
                0xff6d00, 0x53493f, 0x000000, 0x1f042d, 0x000000, 0x725c4f, 0x563861, 0x5a3b34,
                0x3a3a33, 0x000000, 0x525967, 0x525967, 0x4f3252, 0xd3b89a, 0x291717, 0x302020,
                0x191827, 0x545e70, 0x070406, 0x3a2f34, 0x4b3234, 0x593b5b, 0x55395a, 0x2b1e1f,
                0xffffff, 0x474240, 0x000000, 0x1b0228, 0x000000, 0x39453c, 0x352b4a, 0x5a3b34,
                0x3a3a33, 0x000000, 0x4e5664, 0x4f5665, 0x462e4b, 0xd3b89a, 0x25171a, 0x2a1f21,
            },
            new[]
            {
                0.0, 0.029999999329447746, 5.0, 0.0, 0.0033333334140479565, 2.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 2000.0, 1000.0, 0.4000000059604645,
                0.005005359649658203, 0.353485107421875, 0.2843017578125, 0.357940673828125, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.029999999329447746, 5.0, 0.0, 0.0033333334140479565, 2.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 2000.0, 1000.0, 0.4000000059604645,
                0.005005359649658203, 0.353485107421875, 0.2843017578125, 0.357940673828125, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.029999999329447746, 5.0, 0.0, 0.0033333334140479565, 2.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 2000.0, 1000.0, 0.4000000059604645,
                0.005005359649658203, 0.353485107421875, 0.2843017578125, 0.357940673828125, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.05999999865889549, 4.5, 0.0, 0.0033333334140479565, 3.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 2000.0, 1000.0, 0.4000000059604645,
                0.002940654754638672, 1.00372314453125, -0.000732421875, 9.1552734375e-05, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.059166666120290756, 4.541666507720947, 0.0, 0.0033333334140479565, 3.0833332538604736, 0.0,
                0.0, 0.0, 0.0, 0.5, 2000.0, 1000.0, 0.4000000059604645,
                0.0023111500777304173, 0.9467646479606628, 0.02946726605296135, 0.0286966972053051, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.05000000074505806, 5.0, 0.0, 0.0033333334140479565, 4.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 2000.0, 1000.0, 0.4000000059604645,
                -0.004613399505615234, 0.320220947265625, 0.361663818359375, 0.343353271484375, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.05000000074505806, 5.0, 0.0, 0.0033333334140479565, 4.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 2000.0, 1000.0, 0.4000000059604645,
                -0.004611243028193712, 0.32063260674476624, 0.3618282675743103, 0.34221258759498596, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.05000000074505806, 5.0, 0.0, 0.0033333334140479565, 4.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 2000.0, 1000.0, 0.4000000059604645,
                -0.0044193267822265625, 0.357269287109375, 0.37646484375, 0.240692138671875, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.05000000074505806, 5.0, 0.0, 0.0033333334140479565, 3.0, 0.0,
                500.0, 4000.0, 0.0, 0.5, 3000.0, 1000.0, 0.4000000059604645,
                -0.0013418197631835938, 0.307769775390625, 0.51416015625, 0.183807373046875, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.04333333298563957, 5.0, 0.0, 0.0033333334140479565, 2.6666667461395264, 0.0,
                333.33331298828125, 2666.66650390625, 0.0, 0.5, 2666.666748046875, 1000.0, 0.4000000059604645,
                0.0007739067077636719, 0.3230082094669342, 0.4375406801700592, 0.241851806640625, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
            }, 0.20000000298023224, 0);

        // wow_classic_era (the step-3 offline fixture of map 1, map flags [16858641, 1310720, 130]): LightParams 212, the Southfury stand light.
        static readonly FogDay Day212Era = new FogDay(
            new[] { 0, 360, 720, 1440, 2520, 2640 },
            new[]
            {
                0x5d4267, 0x255483, 0x000000, 0x2b2a62, 0x4c466e, 0x8c80a5, 0x59456b, 0x352b4a,
                0xffffff, 0x699ccc, 0x000000, 0x12001d, 0x000000, 0x39453c, 0x352b4a, 0x473c24,
                0x352b4a, 0x323232, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
                0x5d4267, 0x255483, 0x22343a, 0x2b2a62, 0x4c466e, 0x8c80a5, 0x59456b, 0x352b4a,
                0xfffbec, 0xa499cc, 0x000000, 0x12001d, 0x000000, 0x555045, 0x352b4a, 0x5c452c,
                0x352b4a, 0x3b3b3b, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
                0xffc847, 0x676b6b, 0x466975, 0xffab53, 0xff902a, 0xdc5200, 0x794646, 0x5b3b42,
                0xff9e00, 0xff7900, 0x000000, 0x8a1f63, 0x000000, 0x725c4f, 0x563861, 0x724e35,
                0x5b3b42, 0x454545, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
                0xffcc94, 0x8c6f8e, 0xa06394, 0xffe2af, 0xf6b258, 0xc1634e, 0x863d48, 0x843d3f,
                0xfff7de, 0x9c6108, 0x000000, 0x310035, 0x000000, 0x725c4f, 0x563861, 0x9e6147,
                0x71523f, 0x5a5a5a, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
                0xff8c15, 0x7b5d6d, 0x6c4a6c, 0xc28476, 0xfb9861, 0xff502d, 0xaf3b3e, 0x82393e,
                0xff6d00, 0xff4600, 0x000000, 0x501f6d, 0x000000, 0x725c4f, 0x563861, 0x5d452c,
                0x82393e, 0x3c3c3c, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
                0x5d4267, 0x255483, 0x000000, 0x2b2a62, 0x4c466e, 0x8c80a5, 0x59456b, 0x352b4a,
                0xffffff, 0x699ccc, 0x000000, 0x12001d, 0x000000, 0x39453c, 0x352b4a, 0x55422a,
                0x352b4a, 0x383838, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
            },
            new[]
            {
                32000.0, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.5, 0.0, 0.0, 1.0,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                32000.0, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.5, 0.0, 0.0, 1.0,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                32000.0, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.5, 0.0, 0.0, 1.0,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                32000.0, 0.800000011920929, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.5, 0.0, 0.0, 1.0,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                32000.0, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.5, 0.0, 0.0, 1.0,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                32000.0, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.5, 0.0, 0.0, 1.0,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
            }, 0.4000000059604645, 0);
        static readonly long[] MapFlagsEra = { 16858641, 1310720, 130 };

        // wow_classic (the step-3 offline fixture of map 1, map flags [16793181, 135528448, 128]): LightParams 212, the Southfury stand light.
        static readonly FogDay Day212Mop = new FogDay(
            new[] { 0, 360, 720, 1440, 2520, 2640 },
            new[]
            {
                0x362231, 0x508fce, 0x000000, 0x2b2a62, 0x4c466e, 0x8c80a5, 0x59456b, 0x352b4a,
                0xffffff, 0x699ccc, 0x000000, 0x12001d, 0x000000, 0x3b92a1, 0x1d3f69, 0x54442d,
                0x45545e, 0x323232, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
                0x342439, 0x5d97d1, 0x22343a, 0x2b2a62, 0x4c466e, 0x8c80a5, 0x59456b, 0x352b4a,
                0xfffbec, 0xa499cc, 0x000000, 0x12001d, 0x000000, 0x336dae, 0x1d3f69, 0x4f422a,
                0x404b53, 0x3b3b3b, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
                0x785400, 0x989b9b, 0x466975, 0xffab53, 0xff902a, 0xdc5200, 0x794646, 0x5b3b42,
                0xff9e00, 0xff7900, 0x000000, 0x8a1f63, 0x000000, 0x2c48bd, 0x1d3f69, 0x4a4128,
                0x404b67, 0x454545, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
                0x5e3200, 0xb7a5b8, 0xa06394, 0xffe2af, 0xf6b258, 0xc1634e, 0x863d48, 0x673d3f,
                0xfff7de, 0x9c6108, 0x000000, 0x310035, 0x000000, 0x2c5d7b, 0x1d3f69, 0x403e23,
                0x323b42, 0x5a5a5a, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
                0x824200, 0xb39aa7, 0x6c4a6c, 0xc28476, 0xfb9861, 0xff502d, 0xaf3b3e, 0x82393e,
                0xff6d00, 0xff4600, 0x000000, 0x501f6d, 0x000000, 0x3458e9, 0x3458e9, 0x4e422a,
                0x404b67, 0x3c3c3c, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
                0x62456c, 0x698ec4, 0x000000, 0x2b2a62, 0x4c466e, 0x8c80a5, 0x59456b, 0x352b4a,
                0xffffff, 0x699ccc, 0x000000, 0x12001d, 0x000000, 0x3b92a1, 0x20497a, 0x50422b,
                0x323b42, 0x383838, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
            },
            new[]
            {
                32000.0, 0.5, 0.0, 100000.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 1.0,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                32000.0, 0.5, 0.0, 100000.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 1.0,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                32000.0, 0.5, 0.0, 100000.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 1.0,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                32000.0, 0.800000011920929, 0.0, 100000.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 1.0,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                32000.0, 0.5, 0.0, 100000.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 1.0,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
                32000.0, 0.5, 0.0, 100000.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 1.0,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
            }, 0.0, 0);
        static readonly long[] MapFlagsMop = { 16793181, 135528448, 128 };

        // A Retail Z key (FogDensity 0, FogEnd 0): the first of the key export (LightParams 1320), as a one-key day.
        static readonly FogDay DayZ = new FogDay(
            new[] { 0 },
            new[]
            {
                0xd71c1c, 0xe91818, 0xfc0202, 0xd31e1e, 0xe91818, 0xe37676, 0xd13030, 0x000000,
                0xcd1111, 0xbc2d2d, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
                0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
            },
            new[]
            {
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0,
                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0, 0.0,
            }, 0.0, 0);

        // One-key days from 5985's 12:00 key, each with one fall-back predicate set.
        static readonly FogDay[] Synthetic =
        {
            new FogDay( // 5985 12:00 as read
                new[] { 0 },
                new[]
                {
                    0x5e3200, 0xb7a5b8, 0xa06394, 0xffe2af, 0xf6b258, 0xc1634e, 0xb3604d, 0x784b3a,
                    0xfff7de, 0x9c6108, 0x000000, 0x310035, 0x000000, 0x525a5e, 0x373d45, 0x403e23,
                    0x3e362f, 0x000000, 0x9c8a9d, 0x9e7865, 0x955b4e, 0xe5a678, 0x622f38, 0x975649,
                },
                new[]
                {
                    0.0, 0.15000000596046448, 3.0, 30.0, 0.0024999999441206455, 1.0, 0.0,
                    0.0, 0.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                    0.002940654754638672, 1.00372314453125, -0.000732421875, 9.1552734375e-05, 0.0, 0.0, 0.0,
                    0.0, 0.0, 0.0, 0.0, 0.0,
                }, 0.0, 0),
            new FogDay( // EndFogColor zero
                new[] { 0 },
                new[]
                {
                    0x5e3200, 0xb7a5b8, 0xa06394, 0xffe2af, 0xf6b258, 0xc1634e, 0xb3604d, 0x784b3a,
                    0xfff7de, 0x9c6108, 0x000000, 0x310035, 0x000000, 0x525a5e, 0x373d45, 0x403e23,
                    0x3e362f, 0x000000, 0x9c8a9d, 0x9e7865, 0x000000, 0xe5a678, 0x622f38, 0x975649,
                },
                new[]
                {
                    0.0, 0.15000000596046448, 3.0, 30.0, 0.0024999999441206455, 1.0, 0.0,
                    0.0, 0.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                    0.002940654754638672, 1.00372314453125, -0.000732421875, 9.1552734375e-05, 0.0, 0.0, 0.0,
                    0.0, 0.0, 0.0, 0.0, 0.0,
                }, 0.0, 0),
            new FogDay( // FogHeightColor zero
                new[] { 0 },
                new[]
                {
                    0x5e3200, 0xb7a5b8, 0xa06394, 0xffe2af, 0xf6b258, 0xc1634e, 0xb3604d, 0x784b3a,
                    0xfff7de, 0x9c6108, 0x000000, 0x310035, 0x000000, 0x525a5e, 0x373d45, 0x403e23,
                    0x3e362f, 0x000000, 0x9c8a9d, 0x9e7865, 0x955b4e, 0xe5a678, 0x000000, 0x975649,
                },
                new[]
                {
                    0.0, 0.15000000596046448, 3.0, 30.0, 0.0024999999441206455, 1.0, 0.0,
                    0.0, 0.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                    0.002940654754638672, 1.00372314453125, -0.000732421875, 9.1552734375e-05, 0.0, 0.0, 0.0,
                    0.0, 0.0, 0.0, 0.0, 0.0,
                }, 0.0, 0),
            new FogDay( // EndFogHeightColor zero
                new[] { 0 },
                new[]
                {
                    0x5e3200, 0xb7a5b8, 0xa06394, 0xffe2af, 0xf6b258, 0xc1634e, 0xb3604d, 0x784b3a,
                    0xfff7de, 0x9c6108, 0x000000, 0x310035, 0x000000, 0x525a5e, 0x373d45, 0x403e23,
                    0x3e362f, 0x000000, 0x9c8a9d, 0x9e7865, 0x955b4e, 0xe5a678, 0x622f38, 0x000000,
                },
                new[]
                {
                    0.0, 0.15000000596046448, 3.0, 30.0, 0.0024999999441206455, 1.0, 0.0,
                    0.0, 0.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                    0.002940654754638672, 1.00372314453125, -0.000732421875, 9.1552734375e-05, 0.0, 0.0, 0.0,
                    0.0, 0.0, 0.0, 0.0, 0.0,
                }, 0.0, 0),
            new FogDay( // EndFog and EndFogHeight zero
                new[] { 0 },
                new[]
                {
                    0x5e3200, 0xb7a5b8, 0xa06394, 0xffe2af, 0xf6b258, 0xc1634e, 0xb3604d, 0x784b3a,
                    0xfff7de, 0x9c6108, 0x000000, 0x310035, 0x000000, 0x525a5e, 0x373d45, 0x403e23,
                    0x3e362f, 0x000000, 0x9c8a9d, 0x9e7865, 0x000000, 0xe5a678, 0x622f38, 0x000000,
                },
                new[]
                {
                    0.0, 0.15000000596046448, 3.0, 30.0, 0.0024999999441206455, 1.0, 0.0,
                    0.0, 0.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                    0.002940654754638672, 1.00372314453125, -0.000732421875, 9.1552734375e-05, 0.0, 0.0, 0.0,
                    0.0, 0.0, 0.0, 0.0, 0.0,
                }, 0.0, 0),
            new FogDay( // EndFogColorDistance 0
                new[] { 0 },
                new[]
                {
                    0x5e3200, 0xb7a5b8, 0xa06394, 0xffe2af, 0xf6b258, 0xc1634e, 0xb3604d, 0x784b3a,
                    0xfff7de, 0x9c6108, 0x000000, 0x310035, 0x000000, 0x525a5e, 0x373d45, 0x403e23,
                    0x3e362f, 0x000000, 0x9c8a9d, 0x9e7865, 0x955b4e, 0xe5a678, 0x622f38, 0x975649,
                },
                new[]
                {
                    0.0, 0.15000000596046448, 3.0, 30.0, 0.0024999999441206455, 1.0, 0.0,
                    0.0, 0.0, 0.0, 0.5, 0.0, 2100.0, 0.30000001192092896,
                    0.002940654754638672, 1.00372314453125, -0.000732421875, 9.1552734375e-05, 0.0, 0.0, 0.0,
                    0.0, 0.0, 0.0, 0.0, 0.0,
                }, 0.0, 0),
            new FogDay( // SunFogColor zero
                new[] { 0 },
                new[]
                {
                    0x5e3200, 0xb7a5b8, 0xa06394, 0xffe2af, 0xf6b258, 0xc1634e, 0xb3604d, 0x784b3a,
                    0xfff7de, 0x9c6108, 0x000000, 0x310035, 0x000000, 0x525a5e, 0x373d45, 0x403e23,
                    0x3e362f, 0x000000, 0x9c8a9d, 0x9e7865, 0x955b4e, 0x000000, 0x622f38, 0x975649,
                },
                new[]
                {
                    0.0, 0.15000000596046448, 3.0, 30.0, 0.0024999999441206455, 1.0, 0.0,
                    0.0, 0.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                    0.002940654754638672, 1.00372314453125, -0.000732421875, 9.1552734375e-05, 0.0, 0.0, 0.0,
                    0.0, 0.0, 0.0, 0.0, 0.0,
                }, 0.0, 0),
            new FogDay( // coefficients zero, FogHeightScaler set
                new[] { 0 },
                new[]
                {
                    0x5e3200, 0xb7a5b8, 0xa06394, 0xffe2af, 0xf6b258, 0xc1634e, 0xb3604d, 0x784b3a,
                    0xfff7de, 0x9c6108, 0x000000, 0x310035, 0x000000, 0x525a5e, 0x373d45, 0x403e23,
                    0x3e362f, 0x000000, 0x9c8a9d, 0x9e7865, 0x955b4e, 0xe5a678, 0x622f38, 0x975649,
                },
                new[]
                {
                    0.0, 0.15000000596046448, 3.0, 30.0, 0.0024999999441206455, 1.0, 0.0,
                    0.0, 0.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                    0.0, 0.0, 0.0, 0.0, 0.0,
                }, 0.0, 0),
            new FogDay( // FogHeightScaler 0
                new[] { 0 },
                new[]
                {
                    0x5e3200, 0xb7a5b8, 0xa06394, 0xffe2af, 0xf6b258, 0xc1634e, 0xb3604d, 0x784b3a,
                    0xfff7de, 0x9c6108, 0x000000, 0x310035, 0x000000, 0x525a5e, 0x373d45, 0x403e23,
                    0x3e362f, 0x000000, 0x9c8a9d, 0x9e7865, 0x955b4e, 0xe5a678, 0x622f38, 0x975649,
                },
                new[]
                {
                    0.0, 0.15000000596046448, 3.0, 30.0, 0.0, 0.25, 0.0,
                    0.0, 0.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                    0.002940654754638672, 1.00372314453125, -0.000732421875, 9.1552734375e-05, 0.0, 0.0, 0.0,
                    0.0, 0.0, 0.0, 0.0, 0.0,
                }, 0.0, 0),
            new FogDay( // polynomial on
                new[] { 0 },
                new[]
                {
                    0x5e3200, 0xb7a5b8, 0xa06394, 0xffe2af, 0xf6b258, 0xc1634e, 0xb3604d, 0x784b3a,
                    0xfff7de, 0x9c6108, 0x000000, 0x310035, 0x000000, 0x525a5e, 0x373d45, 0x403e23,
                    0x3e362f, 0x000000, 0x9c8a9d, 0x9e7865, 0x955b4e, 0xe5a678, 0x622f38, 0x975649,
                },
                new[]
                {
                    0.0, 0.15000000596046448, 3.0, 30.0, 0.0024999999441206455, 1.0, 0.0,
                    150.0, 2400.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                    0.002940654754638672, 1.00372314453125, -0.000732421875, 9.1552734375e-05, 0.05000000074505806, 0.6000000238418579, 0.20000000298023224,
                    0.10000000149011612, 0.019999999552965164, 0.4000000059604645, 0.30000001192092896, 0.05000000074505806,
                }, 0.0, 0),
            new FogDay( // polynomial refused
                new[] { 0 },
                new[]
                {
                    0x5e3200, 0xb7a5b8, 0xa06394, 0xffe2af, 0xf6b258, 0xc1634e, 0xb3604d, 0x784b3a,
                    0xfff7de, 0x9c6108, 0x000000, 0x310035, 0x000000, 0x525a5e, 0x373d45, 0x403e23,
                    0x3e362f, 0x000000, 0x9c8a9d, 0x9e7865, 0x955b4e, 0xe5a678, 0x622f38, 0x975649,
                },
                new[]
                {
                    0.0, 0.15000000596046448, 3.0, 30.0, 0.0024999999441206455, 1.0, 0.0,
                    1800.0, 900.0, 0.0, 0.5, 900.0, 2100.0, 0.30000001192092896,
                    0.002940654754638672, 1.00372314453125, -0.000732421875, 9.1552734375e-05, 0.05000000074505806, 0.6000000238418579, 0.20000000298023224,
                    0.10000000149011612, 0.019999999552965164, 0.4000000059604645, 0.30000001192092896, 0.05000000074505806,
                }, 0.0, 0),
        };

        // R3: fogpack_ref.py's records (far clip 7000, camera Z 40, the sun of the public path at the half-minute): name, day,
        // half-minute, the sun (Unity), the 56 values, then FogHeight, Start, End, K, Artistic, ArtisticRefusedBlend.
        static readonly PackCase[] PackCases =
        {
            new PackCase("5985 06:49:56", Day5985, 819, new[] { -0.7059247436366739, 0.05779716812260558, 0.7059247436366739 },
                         new[]
                         {
                             450.00001788139343, 7000.0, 0.0012843750000000001, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 10.0,
                             0.3568627450980392, 0.2784313725490196, 0.2784313725490196, 0.0024999999441206455,
                             0.0005, 0.48627450980392156, 0.34901960784313724, 0.3686274509803922,
                             0.0, 0.8980392156862745, 0.6509803921568628, 0.47058823529411764,
                             0.403921568627451, 0.24705882352941178, 0.2196078431372549, 900.0,
                             0.19500000774860382, 0.0, 0.0, 0.0,
                             -0.7059247436366739, 0.05779716812260558, 0.7059247436366739, 0.0,
                             0.18040637969970702, 0.3922462463378906, 0.4326669692993164, 0.0007847666740417481,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.4549019607843137, 0.3254901960784314, 0.2784313725490196, 2100.0,
                         }, 30.0, 450.00001788139343, 7000.0, 0.0012843750000000001, false, false),
            new PackCase("5985 11:53:30", Day5985, 1427, new[] { -0.06573989939311295, 0.9956688863550808, 0.06573989939311295 },
                         new[]
                         {
                             450.00001788139343, 7000.0, 0.001495486111111111, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 10.0,
                             0.4666666666666667, 0.29411764705882354, 0.22745098039215686, 0.0024999999441206455,
                             0.0005, 0.5803921568627451, 0.3568627450980392, 0.3058823529411765,
                             0.0, 0.8980392156862745, 0.6509803921568628, 0.47058823529411764,
                             0.3843137254901961, 0.1843137254901961, 0.2196078431372549, 900.0,
                             0.30000001192092896, 0.0, 0.0, 0.0,
                             -0.06573989939311295, 0.9956688863550808, 0.06573989939311295, 0.0,
                             0.003866259256998711, 0.007494184705946205, 0.991768667432997, 0.0028955234421624075,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.5882352941176471, 0.33725490196078434, 0.28627450980392155, 2100.0,
                         }, 30.0, 450.00001788139343, 7000.0, 0.001495486111111111, false, false),
            new PackCase("5985 18:46:13", Day5985, 2252, new[] { -0.7020557662270837, -0.11931220480154166, 0.7020557662270837 },
                         new[]
                         {
                             450.00001788139343, 7000.0, 0.001048888888888889, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 10.0,
                             0.49019607843137253, 0.23137254901960785, 0.24313725490196078, 0.0024999999441206455,
                             0.0005, 0.7176470588235294, 0.3137254901960784, 0.28627450980392155,
                             0.0, 0.8980392156862745, 0.6509803921568628, 0.47058823529411764,
                             0.3843137254901961, 0.1843137254901961, 0.2196078431372549, 900.0,
                             0.0, 0.0, 0.0, 0.0,
                             -0.7020557662270837, -0.11931220480154166, 0.7020557662270837, 0.0,
                             9.1552734375e-05, -0.000732421875, 1.00372314453125, 0.002940654754638672,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.6196078431372549, 0.3254901960784314, 0.26666666666666666, 2190.222222222222,
                         }, 30.0, 450.00001788139343, 7000.0, 0.001048888888888889, false, false),
            new PackCase("5951 12:00", Day5951, 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             14.999999664723873, 7000.0, 0.00175, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, -85.0,
                             0.16862745098039217, 0.3058823529411765, 0.49019607843137253, 0.0033333334140479565,
                             0.0015, 0.07058823529411765, 0.403921568627451, 0.5647058823529412,
                             0.0, 0.4, 0.6039215686274509, 0.7333333333333333,
                             0.0, 0.13725490196078433, 0.2, 700.0,
                             0.30000001192092896, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             1.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.09019607843137255, 0.3215686274509804, 0.43529411764705883, 400.0,
                         }, 125.0, 14.999999664723873, 7000.0, 0.00175, false, false),
            new PackCase("5944 12:00", Day5944, 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             600.0000089406967, 7000.0, 0.0015, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 40.0,
                             0.30196078431372547, 0.47058823529411764, 0.5607843137254902, 0.0016666667070239782,
                             0.001, 0.45098039215686275, 0.6235294117647059, 0.7098039215686275,
                             0.0, 0.9372549019607843, 0.7725490196078432, 0.5019607843137255,
                             0.19215686274509805, 0.37254901960784315, 0.4666666666666667, 1000.0,
                             0.4000000059604645, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             0.285003662109375, 0.609619140625, 0.120574951171875, -0.003719806671142578,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.2, 0.48627450980392156, 0.6509803921568628, 2000.0,
                         }, 0.0, 600.0000089406967, 7000.0, 0.0015, false, false),
            new PackCase("6033 12:00", Day6033, 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             150.00000223517418, 7000.0, 0.0025, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 40.0,
                             0.45098039215686275, 0.3176470588235294, 0.30980392156862746, 0.0033333334140479565,
                             0.002, 0.6431372549019608, 0.3607843137254902, 0.3176470588235294,
                             0.0, 0.8274509803921568, 0.7215686274509804, 0.6039215686274509,
                             0.4235294117647059, 0.2235294117647059, 0.21568627450980393, 2000.0,
                             0.4000000059604645, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             0.343353271484375, 0.361663818359375, 0.320220947265625, -0.004613399505615234,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.5686274509803921, 0.3137254901960784, 0.30196078431372547, 1000.0,
                         }, 0.0, 150.00000223517418, 7000.0, 0.0025, false, false),
            new PackCase("Z key, LightParams 1320", DayZ, 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             0.0, 7000.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 40.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             1.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 7000.0,
                             1.0, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             1.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.0, 0.0, 0.0, 0.0,
                         }, 0.0, 0.0, 7000.0, 0.0, false, false),
            new PackCase("5985 12:00 as read", Synthetic[0], 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             450.00001788139343, 7000.0, 0.0015, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 10.0,
                             0.47058823529411764, 0.29411764705882354, 0.22745098039215686, 0.0024999999441206455,
                             0.0005, 0.5843137254901961, 0.3568627450980392, 0.3058823529411765,
                             0.0, 0.8980392156862745, 0.6509803921568628, 0.47058823529411764,
                             0.3843137254901961, 0.1843137254901961, 0.2196078431372549, 900.0,
                             0.30000001192092896, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             9.1552734375e-05, -0.000732421875, 1.00372314453125, 0.002940654754638672,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.592156862745098, 0.33725490196078434, 0.28627450980392155, 2100.0,
                         }, 30.0, 450.00001788139343, 7000.0, 0.0015, false, false),
            new PackCase("EndFogColor zero", Synthetic[1], 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             450.00001788139343, 7000.0, 0.0015, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 10.0,
                             0.47058823529411764, 0.29411764705882354, 0.22745098039215686, 0.0024999999441206455,
                             0.0005, 0.47058823529411764, 0.29411764705882354, 0.22745098039215686,
                             0.0, 0.8980392156862745, 0.6509803921568628, 0.47058823529411764,
                             0.3843137254901961, 0.1843137254901961, 0.2196078431372549, 900.0,
                             0.30000001192092896, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             9.1552734375e-05, -0.000732421875, 1.00372314453125, 0.002940654754638672,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.592156862745098, 0.33725490196078434, 0.28627450980392155, 2100.0,
                         }, 30.0, 450.00001788139343, 7000.0, 0.0015, false, false),
            new PackCase("FogHeightColor zero", Synthetic[2], 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             450.00001788139343, 7000.0, 0.0015, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 10.0,
                             0.47058823529411764, 0.29411764705882354, 0.22745098039215686, 0.0024999999441206455,
                             0.0005, 0.5843137254901961, 0.3568627450980392, 0.3058823529411765,
                             0.0, 0.8980392156862745, 0.6509803921568628, 0.47058823529411764,
                             0.47058823529411764, 0.29411764705882354, 0.22745098039215686, 900.0,
                             0.30000001192092896, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             9.1552734375e-05, -0.000732421875, 1.00372314453125, 0.002940654754638672,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.592156862745098, 0.33725490196078434, 0.28627450980392155, 2100.0,
                         }, 30.0, 450.00001788139343, 7000.0, 0.0015, false, false),
            new PackCase("EndFogHeightColor zero", Synthetic[3], 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             450.00001788139343, 7000.0, 0.0015, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 10.0,
                             0.47058823529411764, 0.29411764705882354, 0.22745098039215686, 0.0024999999441206455,
                             0.0005, 0.5843137254901961, 0.3568627450980392, 0.3058823529411765,
                             0.0, 0.8980392156862745, 0.6509803921568628, 0.47058823529411764,
                             0.3843137254901961, 0.1843137254901961, 0.2196078431372549, 900.0,
                             0.30000001192092896, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             9.1552734375e-05, -0.000732421875, 1.00372314453125, 0.002940654754638672,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.5843137254901961, 0.3568627450980392, 0.3058823529411765, 2100.0,
                         }, 30.0, 450.00001788139343, 7000.0, 0.0015, false, false),
            new PackCase("EndFog and EndFogHeight zero", Synthetic[4], 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             450.00001788139343, 7000.0, 0.0015, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 10.0,
                             0.47058823529411764, 0.29411764705882354, 0.22745098039215686, 0.0024999999441206455,
                             0.0005, 0.47058823529411764, 0.29411764705882354, 0.22745098039215686,
                             0.0, 0.8980392156862745, 0.6509803921568628, 0.47058823529411764,
                             0.3843137254901961, 0.1843137254901961, 0.2196078431372549, 900.0,
                             0.30000001192092896, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             9.1552734375e-05, -0.000732421875, 1.00372314453125, 0.002940654754638672,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.47058823529411764, 0.29411764705882354, 0.22745098039215686, 2100.0,
                         }, 30.0, 450.00001788139343, 7000.0, 0.0015, false, false),
            new PackCase("EndFogColorDistance 0", Synthetic[5], 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             450.00001788139343, 7000.0, 0.0015, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 10.0,
                             0.47058823529411764, 0.29411764705882354, 0.22745098039215686, 0.0024999999441206455,
                             0.0005, 0.5843137254901961, 0.3568627450980392, 0.3058823529411765,
                             0.0, 0.8980392156862745, 0.6509803921568628, 0.47058823529411764,
                             0.3843137254901961, 0.1843137254901961, 0.2196078431372549, 7000.0,
                             0.30000001192092896, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             9.1552734375e-05, -0.000732421875, 1.00372314453125, 0.002940654754638672,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.592156862745098, 0.33725490196078434, 0.28627450980392155, 2100.0,
                         }, 30.0, 450.00001788139343, 7000.0, 0.0015, false, false),
            new PackCase("SunFogColor zero", Synthetic[6], 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             450.00001788139343, 7000.0, 0.0015, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 10.0,
                             0.47058823529411764, 0.29411764705882354, 0.22745098039215686, 0.0024999999441206455,
                             0.0005, 0.5843137254901961, 0.3568627450980392, 0.3058823529411765,
                             1.0, 0.0, 0.0, 0.0,
                             0.3843137254901961, 0.1843137254901961, 0.2196078431372549, 900.0,
                             0.30000001192092896, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             9.1552734375e-05, -0.000732421875, 1.00372314453125, 0.002940654754638672,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.592156862745098, 0.33725490196078434, 0.28627450980392155, 2100.0,
                         }, 30.0, 450.00001788139343, 7000.0, 0.0015, false, false),
            new PackCase("coefficients zero, FogHeightScaler set", Synthetic[7], 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             450.00001788139343, 7000.0, 0.0015, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 10.0,
                             0.47058823529411764, 0.29411764705882354, 0.22745098039215686, 0.0024999999441206455,
                             0.0005, 0.5843137254901961, 0.3568627450980392, 0.3058823529411765,
                             0.0, 0.8980392156862745, 0.6509803921568628, 0.47058823529411764,
                             0.3843137254901961, 0.1843137254901961, 0.2196078431372549, 900.0,
                             0.30000001192092896, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             1.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.592156862745098, 0.33725490196078434, 0.28627450980392155, 2100.0,
                         }, 30.0, 450.00001788139343, 7000.0, 0.0015, false, false),
            new PackCase("FogHeightScaler 0", Synthetic[8], 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             450.00001788139343, 7000.0, 0.0015, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 10.0,
                             0.47058823529411764, 0.29411764705882354, 0.22745098039215686, 0.0,
                             0.0015, 0.5843137254901961, 0.3568627450980392, 0.3058823529411765,
                             0.0, 0.8980392156862745, 0.6509803921568628, 0.47058823529411764,
                             0.3843137254901961, 0.1843137254901961, 0.2196078431372549, 900.0,
                             0.30000001192092896, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             9.1552734375e-05, -0.000732421875, 1.00372314453125, 0.002940654754638672,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 1.0,
                             0.592156862745098, 0.33725490196078434, 0.28627450980392155, 2100.0,
                         }, 30.0, 450.00001788139343, 7000.0, 0.0015, false, false),
            new PackCase("polynomial on", Synthetic[9], 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             450.00001788139343, 7000.0, 0.0015, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 10.0,
                             0.47058823529411764, 0.29411764705882354, 0.22745098039215686, 0.0024999999441206455,
                             0.0005, 0.5843137254901961, 0.3568627450980392, 0.3058823529411765,
                             0.0, 0.8980392156862745, 0.6509803921568628, 0.47058823529411764,
                             0.3843137254901961, 0.1843137254901961, 0.2196078431372549, 900.0,
                             0.30000001192092896, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             9.1552734375e-05, -0.000732421875, 1.00372314453125, 0.002940654754638672,
                             0.10000000149011612, 0.20000000298023224, 0.6000000238418579, 0.05000000074505806,
                             0.05000000074505806, 0.30000001192092896, 0.4000000059604645, 0.019999999552965164,
                             2400.0, 150.0, 0.0, 1.0,
                             0.592156862745098, 0.33725490196078434, 0.28627450980392155, 2100.0,
                         }, 30.0, 450.00001788139343, 7000.0, 0.0015, true, false),
            new PackCase("polynomial refused", Synthetic[10], 1440, new[] { -0.06162841671621935, 0.9961946980917455, 0.06162841671621935 },
                         new[]
                         {
                             450.00001788139343, 7000.0, 0.0015, 0.0,
                             0.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 10.0,
                             0.47058823529411764, 0.29411764705882354, 0.22745098039215686, 0.0024999999441206455,
                             0.0005, 0.5843137254901961, 0.3568627450980392, 0.3058823529411765,
                             0.0, 0.8980392156862745, 0.6509803921568628, 0.47058823529411764,
                             0.3843137254901961, 0.1843137254901961, 0.2196078431372549, 900.0,
                             0.30000001192092896, 0.0, 0.0, 0.0,
                             -0.06162841671621935, 0.9961946980917455, 0.06162841671621935, 0.0,
                             9.1552734375e-05, -0.000732421875, 1.00372314453125, 0.002940654754638672,
                             0.10000000149011612, 0.20000000298023224, 0.6000000238418579, 0.05000000074505806,
                             0.05000000074505806, 0.30000001192092896, 0.4000000059604645, 0.019999999552965164,
                             900.0, 1800.0, 1.0, 1.0,
                             0.592156862745098, 0.33725490196078434, 0.28627450980392155, 2100.0,
                         }, 30.0, 450.00001788139343, 7000.0, 0.0015, false, false),
        };
    }
}
