// LiquidLights.cs
//
// The ZONE LIGHT the water and ocean colours come from (Stage 09 tuning): the host's "lights" object inside a load's
// "liquids" offer (WmvIpcClient.LightsFrom), evaluated at a point of the game's world frame, and the ease the player
// applies over frames. Pure C#, no UnityEngine: the player evaluates it at the main camera once a rendered frame and sets
// the liquid shader's globals (WmvLiquidLight); Tests/LiquidLightTests.cs checks it.
//
// The host reads the light tables (Light, LightParams, LightData, ZoneLight, ZoneLightPoint) once per client load and
// sends, per map, the values at a fixed noon (half-minute 1440) for the clear-weather slot 0:
//   zones         the map's ZoneLight polygons, in the order they are tried: those without a player condition first,
//                 then the conditional ones; within each, the smallest area first (polygons nest: an inner zone wins
//                 over the zone around it). Points are (game X north, game Y west) in yards (PROVEN_BYTES); a zone
//                 applies between zmin and zmax.
//   points        the map's Light rows away from the origin that no ZoneLight names: a position (yards, the game
//                 frame) and the falloff start and end (yards, PROVEN_BYTES).
//   defaultLight  the map's own light where no zone applies (0: none).
//   values        per light: river close / far and ocean close / far (bytes 0..255) and the water and ocean alpha pairs
//                 (shallow, deep). A light without water colours (all four 0: most of Classic Beta's rows, among them every
//                 map default and zone light of Eastern Kingdoms and Kalimdor; Retail's Lights 7, 21 and 72, the
//                 Burning Steppes, Badlands and Searing Gorge zone lights) has no entry. Optionally (the High look) the
//                 light's sky colour at the same noon ("sky", bytes 0..255: one of the row's sky columns, the host's
//                 choice): the colour a water reflection that misses the scene takes. A row without it (an older host)
//                 has no sky: the player's neutral sky.
// THE SELECTION (Evaluate; the rule public format documentation describes, INFERRED -- the client's code is not read):
//   1. the base light: the first zone, in the sent order, whose polygon contains (X, Y) -- its bounding box, then the
//      even-odd rule (the data mixes clockwise and counter-clockwise polygons) -- with zmin <= Z <= zmax; else the
//      default light; else none;
//   2. a base light without values (or none) is INVALID: the liquids keep their type colours (the shader's fallback) and
//      no point light is applied;
//   3. point lights with values and a 3D distance d < end weigh w = 1 within start, else (end - d) / (end - start)
//      (end <= start: 1); they are applied weakest first (ties by light id): result = lerp(result, point, w) -- the sky
//      only by point lights that carry one. The result has a sky when its base light has.
// THE EASE (LiquidLightEase): per frame the eased values move by the factor 1 - exp(-dt / 0.25 s) towards the sample;
// they SNAP to it on the first sample, when the lights object changes, when the validity (or whether a sky is given)
// changes, on a move of more than 60 yd in one frame, and whenever the liquid clock is pinned (pinned pictures are
// reproducible). An invalid sample
// turns the zone light off at once. The time constant and the jump distance are documented choices, not the game's.
// Thread safety: an instance is filled once (the reader thread) and then read on the main thread only.

using System;
using System.Collections.Generic;

namespace Wmv.Wow.World
{
    /// <summary>One ZoneLight polygon of the map (the host's order is kept).</summary>
    public sealed class LiquidLightZone
    {
        public int Zone;                          // ZoneLight id
        public int Light;                         // its Light id
        public int Conditional;                   // non-zero: behind a player condition (sent after the others)
        public double ZMin, ZMax;
        /// <summary>x0, y0, x1, y1, ... (game X north, game Y west, yards): 3 points or more.</summary>
        public double[] Poly;
        public double MinX, MaxX, MinY, MaxY;     // the polygon's bounding box

        public int PointCount { get { return Poly.Length / 2; } }

        /// <summary>Whether (x, y) lies inside the polygon: its bounding box, then the even-odd rule (either winding; a
        /// point exactly on an edge may fall either way).</summary>
        public bool Contains(double x, double y)
        {
            if (x < MinX || x > MaxX || y < MinY || y > MaxY)
                return false;
            bool inside = false;
            int n = Poly.Length / 2;
            for (int i = 0, j = n - 1; i < n; j = i++)
            {
                double xi = Poly[2 * i], yi = Poly[2 * i + 1], xj = Poly[2 * j], yj = Poly[2 * j + 1];
                // (An edge with yi == yj is never crossed: no division by zero, repeated points are harmless.)
                if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
                    inside = !inside;
            }
            return inside;
        }
    }

    /// <summary>One point light of the map: a Light row away from the origin that no zone names.</summary>
    public sealed class LiquidLightPoint
    {
        public int Light;
        public double X, Y, Z;                    // the game frame, yards
        public double Start, End;                 // falloff, yards

        /// <summary>The point's weight at 3D distance `d`: 0 at or beyond End; 1 within Start (and wherever it applies
        /// when End &lt;= Start); else linear, (End - d) / (End - Start).</summary>
        public double Weight(double d)
        {
            if (!(d < End))
                return 0.0;
            if (End <= Start || d <= Start)
                return 1.0;
            return (End - d) / (End - Start);
        }
    }

    /// <summary>A light's values at the host's time and slot: the colours as 0..1 (byte / 255) and the alpha pairs, in
    /// LiquidLightSample's layout; the sky only when HasSky (else 0).</summary>
    public sealed class LiquidLightValues
    {
        public int Light, LightParams;
        public bool HasSky;
        public readonly double[] V = new double[LiquidLightSample.Count];
    }

    /// <summary>What the liquids take at one point: the base light (and what gave it) and the nineteen values.</summary>
    public sealed class LiquidLightSample
    {
        /// <summary>The value layout: four rgb colours (0..1, authored), then the water and the ocean (shallow, deep), then
        /// the sky (rgb, 0..1; 0 when HasSky is false).</summary>
        public const int RiverClose = 0, RiverFar = 3, OceanClose = 6, OceanFar = 9, WaterAlpha = 12, OceanAlpha = 14, Sky = 16, Count = 19;
        public const string WhyNoLight = "no zone contains the point and the map has no default light";
        public const string WhyNoValues = "the base light has no water colours";

        public bool Valid;
        public int Light;                         // the base light, 0 = none
        public int Zone = -1;                     // the zone that gave it (ZoneLight id), 0 = the map's default light, -1 = none
        public int LightParams;                   // the base light's params (slot 0), 0 when invalid
        public int Points;                        // point lights applied
        public string Why = "";                   // why it is invalid ("" when valid)
        public bool HasSky;                       // the base light gives a sky colour
        public readonly double[] V = new double[Count];
    }

    /// <summary>The host's "lights" object for one map (see the file header).</summary>
    public sealed class LiquidLights
    {
        public string Product = "", Build = "", DefaultFrom = "";
        public int Map, Time, Slot, DefaultLight;
        public readonly List<LiquidLightZone> Zones = new List<LiquidLightZone>();
        public readonly List<LiquidLightPoint> Points = new List<LiquidLightPoint>();
        readonly Dictionary<int, LiquidLightValues> values = new Dictionary<int, LiquidLightValues>();
        readonly List<KeyValuePair<double, LiquidLightValues>> applied = new List<KeyValuePair<double, LiquidLightValues>>(8);  // Evaluate's
        /// <summary>Entries the Add methods refused (too few points, an odd coordinate count, short arrays, not finite).</summary>
        public int DroppedZones, DroppedPoints, DroppedValues;

        public int ValueCount { get { return values.Count; } }

        static bool Finite(double d) { return !double.IsNaN(d) && !double.IsInfinity(d); }

        /// <summary>Appends a zone (tried in the order added); false, and counted dropped, when its polygon has fewer than 3
        /// points or an odd coordinate count, or a value is not finite.</summary>
        public bool AddZone(int zone, int light, double zmin, double zmax, int conditional, double[] poly)
        {
            bool ok = poly != null && poly.Length >= 6 && poly.Length % 2 == 0 && Finite(zmin) && Finite(zmax);
            if (ok)
                foreach (double c in poly)
                    ok &= Finite(c);
            if (!ok)
            {
                DroppedZones++;
                return false;
            }
            var z = new LiquidLightZone
            {
                Zone = zone, Light = light, Conditional = conditional, ZMin = zmin, ZMax = zmax, Poly = (double[])poly.Clone(),
                MinX = double.PositiveInfinity, MaxX = double.NegativeInfinity, MinY = double.PositiveInfinity, MaxY = double.NegativeInfinity,
            };
            for (int k = 0; k < poly.Length; k += 2)
            {
                z.MinX = Math.Min(z.MinX, poly[k]);
                z.MaxX = Math.Max(z.MaxX, poly[k]);
                z.MinY = Math.Min(z.MinY, poly[k + 1]);
                z.MaxY = Math.Max(z.MaxY, poly[k + 1]);
            }
            Zones.Add(z);
            return true;
        }

        /// <summary>Appends a point light; false, and counted dropped, when a value is not finite.</summary>
        public bool AddPoint(int light, double x, double y, double z, double start, double end)
        {
            if (!Finite(x) || !Finite(y) || !Finite(z) || !Finite(start) || !Finite(end))
            {
                DroppedPoints++;
                return false;
            }
            Points.Add(new LiquidLightPoint { Light = light, X = x, Y = y, Z = z, Start = start, End = end });
            return true;
        }

        /// <summary>A light's values (colours as bytes 0..255, alphas as given); a later entry for the same light replaces the
        /// earlier one. False, and counted dropped, when a colour has fewer than 3 values, an alpha pair fewer than 2, or a
        /// value is not finite. The sky (bytes) is optional: absent (null or empty, as an older host's rows parse), short or
        /// not finite, the light has no sky and the row is kept.</summary>
        public bool AddValues(int light, int lightParams, float[] riverClose, float[] riverFar, float[] oceanClose, float[] oceanFar,
                              float[] waterAlpha, float[] oceanAlpha, float[] sky = null)
        {
            var v = new LiquidLightValues { Light = light, LightParams = lightParams };
            bool ok = Put(v.V, LiquidLightSample.RiverClose, riverClose, 3, 255.0) && Put(v.V, LiquidLightSample.RiverFar, riverFar, 3, 255.0) &&
                      Put(v.V, LiquidLightSample.OceanClose, oceanClose, 3, 255.0) && Put(v.V, LiquidLightSample.OceanFar, oceanFar, 3, 255.0) &&
                      Put(v.V, LiquidLightSample.WaterAlpha, waterAlpha, 2, 1.0) && Put(v.V, LiquidLightSample.OceanAlpha, oceanAlpha, 2, 1.0);
            if (!ok)
            {
                DroppedValues++;
                return false;
            }
            v.HasSky = Put(v.V, LiquidLightSample.Sky, sky, 3, 255.0);
            if (!v.HasSky)
                Array.Clear(v.V, LiquidLightSample.Sky, 3);
            values[light] = v;
            return true;
        }

        static bool Put(double[] into, int at, float[] from, int n, double divisor)
        {
            if (from == null || from.Length < n)
                return false;
            for (int k = 0; k < n; k++)
            {
                if (!Finite(from[k]))
                    return false;
                into[at + k] = from[k] / divisor;
            }
            return true;
        }

        public bool TryGetValues(int light, out LiquidLightValues v)
        {
            return values.TryGetValue(light, out v);
        }

        /// <summary>The light at game point (x north, y west, z up), yards (see the file header's selection).</summary>
        public LiquidLightSample Evaluate(double x, double y, double z)
        {
            var s = new LiquidLightSample();
            Evaluate(x, y, z, s);
            return s;
        }

        /// <summary>Evaluate into `s`, without allocating (one thread at a time: the point-light list is reused).</summary>
        public void Evaluate(double x, double y, double z, LiquidLightSample s)
        {
            if (s == null)
                throw new ArgumentNullException("s");
            s.Valid = false;
            s.Light = 0;
            s.Zone = -1;
            s.LightParams = 0;
            s.Points = 0;
            s.Why = "";
            s.HasSky = false;
            Array.Clear(s.V, 0, LiquidLightSample.Count);
            LiquidLightZone hit = null;
            foreach (LiquidLightZone zone in Zones)
                if (z >= zone.ZMin && z <= zone.ZMax && zone.Contains(x, y))
                {
                    hit = zone;
                    break;
                }
            if (hit != null)
            {
                s.Zone = hit.Zone;
                s.Light = hit.Light;
            }
            else if (DefaultLight > 0)
            {
                s.Zone = 0;
                s.Light = DefaultLight;
            }
            else
            {
                s.Why = LiquidLightSample.WhyNoLight;
                return;
            }
            LiquidLightValues b;
            if (!values.TryGetValue(s.Light, out b))
            {
                s.Why = LiquidLightSample.WhyNoValues;
                return;
            }
            s.Valid = true;
            s.LightParams = b.LightParams;
            s.HasSky = b.HasSky;
            Array.Copy(b.V, s.V, LiquidLightSample.Count);

            // Weakest first, ties by light id: the strongest has the last word. Kept in order by insertion into the reused
            // list (a handful of lights at most), so a frame near point lights allocates nothing.
            applied.Clear();
            foreach (LiquidLightPoint p in Points)
            {
                double dx = x - p.X, dy = y - p.Y, dz = z - p.Z;
                double w = p.Weight(Math.Sqrt(dx * dx + dy * dy + dz * dz));
                LiquidLightValues pv;
                if (w <= 0.0 || !values.TryGetValue(p.Light, out pv))
                    continue;
                int at = applied.Count;
                while (at > 0 && (w < applied[at - 1].Key || (w == applied[at - 1].Key && pv.Light < applied[at - 1].Value.Light)))
                    at--;
                applied.Insert(at, new KeyValuePair<double, LiquidLightValues>(w, pv));
            }
            if (applied.Count == 0)
                return;
            foreach (KeyValuePair<double, LiquidLightValues> kv in applied)
            {
                // (The sky only from a point light that carries one: a light without it leaves the sky as it is.)
                int n = kv.Value.HasSky ? LiquidLightSample.Count : LiquidLightSample.Sky;
                for (int k = 0; k < n; k++)
                    s.V[k] += (kv.Value.V[k] - s.V[k]) * kv.Key;
            }
            s.Points = applied.Count;
        }

        /// <summary>The object in a line (the log).</summary>
        public string Summary()
        {
            return string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                 "map {0} ({1} {2}), time {3} slot {4}: {5} zone(s), {6} point light(s), {7} light(s) with values, default light {8} " +
                                 "(from {9}){10}", Map, Product, Build, Time, Slot, Zones.Count, Points.Count, values.Count, DefaultLight,
                                 DefaultFrom.Length > 0 ? DefaultFrom : "-",
                                 DroppedZones + DroppedPoints + DroppedValues > 0
                                     ? string.Format("; dropped {0} zone(s), {1} point(s), {2} value row(s)", DroppedZones, DroppedPoints, DroppedValues)
                                     : "");
        }
    }

    /// <summary>
    /// The eased values the shader is given (see the file header): Step once a rendered frame with the frame's sample.
    /// </summary>
    public sealed class LiquidLightEase
    {
        /// <summary>The ease's time constant (seconds): a documented choice.</summary>
        public const double TimeConstant = 0.25;
        /// <summary>A camera move of more than this many yards in one frame snaps (a teleport, not a flight: the free
        /// camera's top speed is 120 yd/s).</summary>
        public const double SnapDistance = 60.0;
        /// <summary>Step's answers: the snap reasons, or "invalid" (nothing to show: the type colours at once).</summary>
        public const string SnapFirst = "first", SnapLights = "lights", SnapValidity = "validity", SnapPinned = "pinned", SnapJump = "jump",
                            Invalid = "invalid";

        public bool Has;                          // a sample was taken since the last Reset
        public bool Valid;                        // the last sample's validity
        public bool HasSky;                       // the last (valid) sample gave a sky colour
        public object Source;                     // the lights object of the last sample
        public double X, Y, Z;                    // where it was taken
        public int Light, Zone, LightParams;      // the last sample's base light (not eased)
        public readonly double[] V = new double[LiquidLightSample.Count];

        /// <summary>The share of the way to the target one frame of `dt` seconds moves: 1 - exp(-dt / TimeConstant); 0 for
        /// no time (or a time that is not a number).</summary>
        public static double Factor(double dt)
        {
            return dt > 0.0 ? 1.0 - Math.Exp(-dt / TimeConstant) : 0.0;
        }

        /// <summary>The snap reason for a sample of `source` with validity `valid` (and a sky or not) at (x, y, z) (null: it
        /// eases).</summary>
        public string SnapReason(object source, bool valid, double x, double y, double z, bool pinned, bool hasSky = false)
        {
            if (!Has)
                return SnapFirst;
            if (!ReferenceEquals(source, Source))
                return SnapLights;
            if (valid != Valid || (valid && hasSky != HasSky))
                return SnapValidity;
            if (pinned)
                return SnapPinned;
            double dx = x - X, dy = y - Y, dz = z - Z;
            if (dx * dx + dy * dy + dz * dz > SnapDistance * SnapDistance)
                return SnapJump;
            return null;
        }

        /// <summary>One frame: the sample `target` of lights `source` at (x, y, z), `dt` seconds after the last, the clock
        /// pinned or not. Returns null when the values eased, the snap reason when they snapped, or Invalid (the values are
        /// kept, unused: an invalid sample shows the type colours at once).</summary>
        public string Step(LiquidLightSample target, object source, double x, double y, double z, double dt, bool pinned)
        {
            if (target == null)
                throw new ArgumentNullException("target");
            string snap = SnapReason(source, target.Valid, x, y, z, pinned, target.HasSky);
            Has = true;
            Source = source;
            Valid = target.Valid;
            HasSky = target.Valid && target.HasSky;
            X = x;
            Y = y;
            Z = z;
            Light = target.Light;
            Zone = target.Zone;
            LightParams = target.LightParams;
            if (!target.Valid)
                return Invalid;
            double f = snap != null ? 1.0 : Factor(dt);
            for (int k = 0; k < LiquidLightSample.Count; k++)
                V[k] = snap != null ? target.V[k] : V[k] + (target.V[k] - V[k]) * f;
            return snap;
        }

        /// <summary>Forget everything: the next sample snaps.</summary>
        public void Reset()
        {
            Has = false;
            Valid = false;
            HasSky = false;
            Source = null;
            Light = LightParams = 0;
            Zone = -1;
        }
    }
}
