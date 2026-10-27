// LiquidWaterDay.cs
//
// THE WATER'S VALUES ON THE LIGHT'S CLOCK (Stage 10 step 8, docs/world-viewer/10-light-sky-fog.md 6.11, 13.3): the table of
// light values the water's selection (LiquidLights.Evaluate, its table overload) reads while the outdoor light is shown, at
// the light's half-minute t instead of the host's fixed noon. Pure C#, no UnityEngine: WmvLiquidLight builds it once per change
// of its inputs; Tests/LiquidWaterDayTests.cs checks it, the 12:00 invariant included on the host's own dumps.
//
// One table per (lights, set, t, clamp), rebuilt only when one of the four changes (the steady state allocates nothing: the
// per-light values are made once and reused). For every light with a Stage 09 row (LiquidLights.Rows: the host's "values",
// which carry the alphas) -- and only those:
//   the colours   the four water colours of the light's slot-0 LightParams day in the set (WorldLightSet.ParamsOf(light, 0):
//                 its slots, slot 0's LightParams, its day), RiverClose, RiverFar, OceanClose and OceanFar at t, each channel as
//                 the host rounds it (WorldLightDay.Colour: byte parity with the host's sample(), PROVEN_SOURCE) and as byte /
//                 255.0, exactly as LiquidLights.AddValues divides the row's bytes
//   all four 0    no entry at t: Stage 09's rule ("a light without water colours has no values") applied at t, so the light is
//                 invalid for the water there (a base light: the type colours; a point light: skipped). The census found such
//                 spans on maps other than 0 and 1 only (Retail/PTR 5 lights near 18:00, Era/Beta 1 light near dawn; PROVEN_BYTES)
//   the alphas    the row's (time-independent LightParams columns the worldLight object does not carry; a copy row's equal its
//                 source's, census Q5, PROVEN_BYTES)
//   the sky       the row's noon SkyBand1 (and HasSky): unused by the shader while the dome sky is drawn, kept so the 12:00
//                 invariant covers all nineteen values
// A light whose day the set lacks (no slots, no slot-0 LightParams, no keys), or whose slot-0 LightParams is not the row's, keeps
// its Stage 09 row (noon) and is counted (NoonFallbacks). So does every row light of a set that lacks one of the four water
// colour columns (its layout's absentColumns, or not sent: WorldLightSet.ColourPresent): such a column reads 0 in every key,
// which would make lights invalid that Stage 09 shows (as step 7 draws no fog for an object without its fog columns; the host
// sends every column on the audited clients). A light without a row never gets an entry: the census found no
// (map, light) whose water colours are 0 at noon and not 0 at another time on any of the five clients (Q1 = 0, PROVEN_BYTES),
// so nothing the clock could show is lost.
// THE 12:00 INVARIANT: at t = 1440 every entry equals its row (same keys, the nineteen doubles bit-identical: the census found
// the player's day at 1440 equal to the host's noon bytes on every row light, Q4), hence every evaluation equals Stage 09's.
// Thread safety: one thread at a time (the main thread).

using System;
using System.Collections.Generic;

namespace Wmv.Wow.World
{
    /// <summary>The water's light values at one half-minute of the outdoor light's clock (see the file header).</summary>
    public sealed class LiquidWaterDay
    {
        /// <summary>The host's fixed time (half-minutes): Stage 09's noon.</summary>
        public const int Noon = 1440;

        /// <summary>The four water colour columns, in LiquidLightSample's order (RiverClose, RiverFar, OceanClose, OceanFar).</summary>
        static readonly int[] Columns =
        {
            WorldLightColumns.RiverCloseColor, WorldLightColumns.RiverFarColor, WorldLightColumns.OceanCloseColor, WorldLightColumns.OceanFarColor,
        };
        static readonly int[] At = { LiquidLightSample.RiverClose, LiquidLightSample.RiverFar, LiquidLightSample.OceanClose, LiquidLightSample.OceanFar };

        LiquidLights lights;
        WorldLightSet set;
        int halfMinute = int.MinValue;
        bool clamp, built;
        string absentColumns;          // the set's water colour columns it lacks (null: none), found once per set
        readonly Dictionary<int, LiquidLightValues> table = new Dictionary<int, LiquidLightValues>();
        // Each light's own values, made the first time the light is evaluated from its day and reused by every rebuild.
        readonly Dictionary<int, LiquidLightValues> made = new Dictionary<int, LiquidLightValues>();
        readonly int[] rgb = new int[3];

        /// <summary>The table (light id -> values) for LiquidLights.Evaluate's table overload.</summary>
        public Dictionary<int, LiquidLightValues> Table { get { return table; } }
        public LiquidLights Lights { get { return lights; } }
        public WorldLightSet Set { get { return set; } }
        public int HalfMinute { get { return halfMinute; } }
        public bool Clamp { get { return clamp; } }
        /// <summary>The water colour columns the set lacks, "RiverFarColor, OceanFarColor" (null: none): every row light is kept at
        /// noon then.</summary>
        public string AbsentColumns { get { return absentColumns; } }
        /// <summary>The last build's counts: rows read, entries from a day, rows kept at noon (no day, or another slot-0 LightParams),
        /// of which another LightParams, rows with all four colours 0 at t (no entry).</summary>
        public int Rows, FromDay, NoonFallbacks, ParamsMismatches, ZeroAtTime;
        /// <summary>Builds made (the cache's check: a frame on the same inputs makes none).</summary>
        public int Builds;

        /// <summary>The table for `l` and `s` at half-minute `t` (wrapped into the day), the day clamped at its ends or not (the
        /// -wmvLightWrap arm); rebuilt only when one of them changed. True when it was rebuilt. A null `l` or `s` empties it.</summary>
        public bool Update(LiquidLights l, WorldLightSet s, int t, bool clampDay)
        {
            t = ((t % WorldLightDay.Day) + WorldLightDay.Day) % WorldLightDay.Day;
            if (built && ReferenceEquals(l, lights) && ReferenceEquals(s, set) && t == halfMinute && clampDay == clamp)
                return false;
            if (!ReferenceEquals(l, lights) || !ReferenceEquals(s, set))
                made.Clear();
            if (!built || !ReferenceEquals(s, set))
                absentColumns = AbsentOf(s);
            lights = l;
            set = s;
            halfMinute = t;
            clamp = clampDay;
            built = true;
            Build();
            return true;
        }

        /// <summary>Forget the inputs (a scene went): the next Update builds, and no light's values are kept.</summary>
        public void Reset()
        {
            lights = null;
            set = null;
            halfMinute = int.MinValue;
            built = false;
            absentColumns = null;
            table.Clear();
            made.Clear();
            Rows = FromDay = NoonFallbacks = ParamsMismatches = ZeroAtTime = 0;
        }

        void Build()
        {
            table.Clear();
            Rows = FromDay = NoonFallbacks = ParamsMismatches = ZeroAtTime = 0;
            Builds++;
            if (lights == null || set == null)
                return;
            foreach (LiquidLightValues row in lights.Rows)
            {
                Rows++;
                if (absentColumns != null)
                {
                    NoonFallbacks++;           // the set lacks a water colour column: the row (noon) as Stage 09 has it
                    table[row.Light] = row;
                    continue;
                }
                bool fellBack;
                WorldLightParams p = set.ParamsOf(row.Light, 0, out fellBack);
                WorldLightDay day = p != null ? set.DayAt(p.Day) : null;
                if (day == null || p.Id != row.LightParams)
                {
                    // No day for the light in the set, or its slot-0 LightParams is not the row's: the row (noon) as Stage 09 has it.
                    NoonFallbacks++;
                    if (day != null)
                        ParamsMismatches++;
                    table[row.Light] = row;
                    continue;
                }
                LiquidLightValues v;
                if (!made.TryGetValue(row.Light, out v))
                {
                    v = new LiquidLightValues();
                    made[row.Light] = v;
                }
                bool any = false;
                for (int c = 0; c < Columns.Length; c++)
                {
                    day.Colour(halfMinute, Columns[c], clamp, out rgb[0], out rgb[1], out rgb[2]);
                    for (int k = 0; k < 3; k++)
                    {
                        any |= rgb[k] != 0;
                        v.V[At[c] + k] = rgb[k] / 255.0;
                    }
                }
                if (!any)
                {
                    ZeroAtTime++;              // Stage 09's rule at t: no water colours, no entry
                    continue;
                }
                v.Light = row.Light;
                v.LightParams = row.LightParams;
                v.HasSky = row.HasSky;
                for (int k = LiquidLightSample.WaterAlpha; k < LiquidLightSample.Count; k++)
                    v.V[k] = row.V[k];         // the alphas and the sky: the row's
                table[row.Light] = v;
                FromDay++;
            }
        }

        /// <summary>The water colour columns `s` lacks (ColourPresent), as "RiverCloseColor, OceanFarColor"; null when it has all four
        /// or for no set.</summary>
        static string AbsentOf(WorldLightSet s)
        {
            if (s == null)
                return null;
            string absent = null;
            foreach (int c in Columns)
                if (!s.ColourPresent(c))
                    absent = (absent == null ? "" : absent + ", ") + WorldLightColumns.Colours[c];
            return absent;
        }

        /// <summary>The selection at game point (x, y, z) on this table (LiquidLights.Evaluate's table overload), into `s`.</summary>
        public void Evaluate(double x, double y, double z, LiquidLightSample s)
        {
            if (lights == null)
                throw new InvalidOperationException("LiquidWaterDay.Evaluate before Update with lights");
            lights.Evaluate(x, y, z, s, table);
        }

        /// <summary>The half-minute as the log says it: hh:mm, with :30 on an odd half-minute.</summary>
        public static string Clock(int t)
        {
            t = ((t % WorldLightDay.Day) + WorldLightDay.Day) % WorldLightDay.Day;
            int h = t / 120, m = (t % 120) / 2;
            return h.ToString("00", System.Globalization.CultureInfo.InvariantCulture) + ":" + m.ToString("00", System.Globalization.CultureInfo.InvariantCulture) +
                   ((t & 1) != 0 ? ":30" : "");
        }

        /// <summary>The last build in a line (the log).</summary>
        public string Summary()
        {
            return string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                 "t {0} ({1}{2}): {3} row(s), {4} from the light's day, {5} kept at noon ({6} with another slot-0 LightParams), {7} without " +
                                 "water colours at t{8}", halfMinute, Clock(halfMinute), clamp ? ", day clamped" : "", Rows, FromDay, NoonFallbacks, ParamsMismatches,
                                 ZeroAtTime, absentColumns != null ? "; water colour columns absent: " + absentColumns : "");
        }
    }
}
