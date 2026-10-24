// WorldLight.cs
//
// THE OUTDOOR LIGHT'S DATA (Stage 10, docs/world-viewer/10-light-sky-fog.md 6.3-6.4): the host's "worldLight" object of a
// terrain or world load (protocol 20, WmvIpcClient.WorldLightFrom), and the selection of the light at a point of the game's
// world frame. Pure C#, no UnityEngine: the player evaluates it at the main camera once a rendered frame (WmvWorldLight,
// WorldLightState.cs); Tests/WorldLightTests.cs checks it. Every table value is the client's own (PROVEN_BYTES per column, in
// the host's dbd files); the column names are the public ones (INFERRED (public), the owner's consent D1).
//
// The object (one per map, made by the host on the map's first load and kept; a streaming session's tiles keep the entry's):
//   zones          the map's ZoneLight polygons in Stage 09's order (the unconditional ones first, then the conditional ones;
//                  within each the smallest area first, ties by id), each with its Light, its ZoneLight.TransitionType
//                  ("priority", public name, 0 where the layout has none) and its Z band
//   points         Stage 09's point lights: the map's Light rows away from the origin that no ZoneLight names
//   defaultLight   Stage 09's default light (the map's origin row whose falloff end is 0, else its lowest origin row, else
//                  Light 1; "none": 0)
//   lights         every Light the zones, points and default name: its eight LightParams ids (0: none)
//   params         every LightParams in slot 0 or 1 of those lights: its day (an index into "days": its own keys, else its
//                  copy source's; -1: none), Glow, its LightSkybox, Flags, HighlightSky and the sun overrides (as read)
//   columns        the order of every day's colour, float and integer columns, once (colorColumns, floatColumns,
//                  intColumns), and the columns this client's layout lacks (absentColumns, sent as 0)
//   days           the keys of a day: times in half-minutes [0, 2880), sorted, each once; colours 0xRRGGBB (24 bits), floats
//                  as read, integers; key-major (key 0's columns first)
//   timeOverride   Map.TimeOfDayOverride (the repository's Map.dbd Code12): -1 none, else MINUTES (INFERRED)
// THE SELECTION (Select): Stage 09's rules (INFERRED; LightGeometry.cs) -- the first zone in the sent order whose polygon holds
// (X, Y) with zmin <= Z <= zmax, else the default light, else none; the base light's LightParams of the slot asked for (slot
// 0, clear weather: INFERRED (public)) -- a slot other than 0 without keys falls back to slot 0 (INFERRED) --; a base light
// without keys is INVALID (no point light is applied); point lights whose LightParams have keys, weighted by their 3D
// distance (1 within the falloff start, linear to 0 at the end), applied weakest first, ties by light id. VALIDITY is "the
// day has keys", per channel group (WorldLightSample), never the water's rule (Stage 09's "liquids.lights" stays the water's).
// The public alternatives exist only as debug arms (WorldLightRules: strongest- or farthest-first points, 2D distance, a
// day clamped at its ends instead of wrapped); a default changes only after an in-game test names the mechanism.
// THE DAY (WorldLightDay.Evaluate): on the integer half-minute t, the key at t, else linear between the last key before t
// and the first after it, across midnight (wrap: INFERRED (public), two sources); colours per channel
// floor(a + (b - a) f + 0.5) clamped to 0..255 -- exactly the host's LightCatalog::sample() -- floats linear, integers the
// earlier key's; one key holds all day. Stage 10 step 7: each key's derived fog channels (WorldFog.DeriveKey: the fall-backs
// only the fog record uses, resolved on the key itself) are made once with the day and blended with the same bracket and
// share, their colours with the same rounding.
// Thread safety: an instance is filled once (the reader thread) and then read on the main thread only.

using System;
using System.Collections.Generic;
using System.Globalization;

namespace Wmv.Wow.World
{
    /// <summary>The columns the player knows, in its own order (the spec's lists): what a day's sent columns are mapped to.</summary>
    public static class WorldLightColumns
    {
        public static readonly string[] Colours =
        {
            "DirectColor", "AmbientColor", "SkyTopColor", "SkyMiddleColor", "SkyBand1Color", "SkyBand2Color", "SkySmogColor", "SkyFogColor",
            "SunColor", "CloudSunColor", "CloudEmissiveColor", "CloudLayer1AmbientColor", "CloudLayer2AmbientColor", "OceanCloseColor",
            "OceanFarColor", "RiverCloseColor", "RiverFarColor", "ShadowOpacity", "HorizonAmbientColor", "GroundAmbientColor", "EndFogColor",
            "SunFogColor", "FogHeightColor", "EndFogHeightColor",
        };
        public const int DirectColor = 0, AmbientColor = 1, SkyTopColor = 2, SkyMiddleColor = 3, SkyBand1Color = 4, SkyBand2Color = 5,
                         SkySmogColor = 6, SkyFogColor = 7, SunColor = 8, CloudSunColor = 9, CloudEmissiveColor = 10, CloudLayer1AmbientColor = 11,
                         CloudLayer2AmbientColor = 12, OceanCloseColor = 13, OceanFarColor = 14, RiverCloseColor = 15, RiverFarColor = 16,
                         ShadowOpacity = 17, HorizonAmbientColor = 18, GroundAmbientColor = 19, EndFogColor = 20, SunFogColor = 21,
                         FogHeightColor = 22, EndFogHeightColor = 23;

        public static readonly string[] Floats =
        {
            "FogEnd", "FogScaler", "FogDensity", "FogHeight", "FogHeightScaler", "FogHeightDensity", "FogZScalar", "MainFogStartDist",
            "MainFogEndDist", "SunFogAngle", "CloudDensity", "EndFogColorDistance", "FogStartOffset", "SunFogStrength",
            "FogHeightCoefficients0", "FogHeightCoefficients1", "FogHeightCoefficients2", "FogHeightCoefficients3",
            "MainFogCoefficients0", "MainFogCoefficients1", "MainFogCoefficients2", "MainFogCoefficients3",
            "HeightDensityFogCoeff0", "HeightDensityFogCoeff1", "HeightDensityFogCoeff2", "HeightDensityFogCoeff3",
        };
        public const int FogEnd = 0, FogScaler = 1, FogDensity = 2, FogHeight = 3, FogHeightScaler = 4, FogHeightDensity = 5, FogZScalar = 6,
                         MainFogStartDist = 7, MainFogEndDist = 8, SunFogAngle = 9, CloudDensity = 10, EndFogColorDistance = 11,
                         FogStartOffset = 12, SunFogStrength = 13, FogHeightCoefficients = 14, MainFogCoefficients = 18,
                         HeightDensityFogCoeff = 22;

        public static readonly string[] Ints = { "ColorGradingFileDataID", "DarkerColorGradingFileDataID" };
        public const int ColorGradingFileDataID = 0, DarkerColorGradingFileDataID = 1;

        public static int ColourCount { get { return Colours.Length; } }
        public static int FloatCount { get { return Floats.Length; } }
        public static int IntCount { get { return Ints.Length; } }

        /// <summary>Where each sent column goes in the player's order (-1: a column the player does not know, ignored).</summary>
        public static int[] Map(string[] sent, string[] known)
        {
            var map = new int[sent != null ? sent.Length : 0];
            for (int k = 0; k < map.Length; k++)
                map[k] = sent[k] != null ? Array.IndexOf(known, sent[k]) : -1;
            return map;
        }
    }

    /// <summary>One day of a LightParams: its keys, every column in the player's order (WorldLightColumns).</summary>
    public sealed class WorldLightDay
    {
        public const int Day = 2880;

        public readonly int[] Times;          // half-minutes, sorted, each once, [0, Day)
        public readonly int[] Colours;        // Keys x ColourCount, 0xRRGGBB
        public readonly double[] Floats;      // Keys x FloatCount, as sent
        public readonly long[] Ints;          // Keys x IntCount
        /// <summary>Keys x WorldFog.DerivedCount: each key's derived fog channels (WorldFog.DeriveKey; the colours as bytes).</summary>
        public readonly double[] Fog;

        public int Keys { get { return Times.Length; } }

        WorldLightDay(int[] times, int[] colours, double[] floats, long[] ints)
        {
            Times = times;
            Colours = colours;
            Floats = floats;
            Ints = ints;
            Fog = new double[times.Length * WorldFog.DerivedCount];
        }

        /// <summary>A day from the sent arrays (key-major, `colourCount` / `floatCount` / `intCount` columns per key), each
        /// column put where `colourMap` / `floatMap` / `intMap` say (-1: dropped); a column the player knows that was not
        /// sent stays 0. Null, with why, when there is no key, the times are not sorted and unique within the day, an array
        /// is short, or a float is not finite.</summary>
        public static WorldLightDay From(int[] times, int[] c, double[] f, long[] i, int[] colourMap, int[] floatMap, int[] intMap,
                                         out string why)
        {
            why = null;
            int n = times != null ? times.Length : 0;
            int nc = colourMap != null ? colourMap.Length : 0, nf = floatMap != null ? floatMap.Length : 0, ni = intMap != null ? intMap.Length : 0;
            if (n == 0)
                why = "no keys";
            else if ((c != null ? c.Length : 0) < n * nc || (f != null ? f.Length : 0) < n * nf || (i != null ? i.Length : 0) < n * ni)
                why = "an array is shorter than its keys x columns";
            else
                for (int k = 0; k < n && why == null; k++)
                    if (times[k] < 0 || times[k] >= Day || (k > 0 && times[k] <= times[k - 1]))
                        why = "the times are not sorted, each once, within the day";
            if (why == null)
                for (int k = 0; k < n * nf && why == null; k++)
                    if (!LightGeometry.Finite(f[k]))
                        why = "a float is not finite";
            if (why != null)
                return null;
            int cc = WorldLightColumns.ColourCount, fc = WorldLightColumns.FloatCount, ic = WorldLightColumns.IntCount;
            var d = new WorldLightDay((int[])times.Clone(), new int[n * cc], new double[n * fc], new long[n * ic]);
            for (int k = 0; k < n; k++)
            {
                for (int j = 0; j < nc; j++)
                    if (colourMap[j] >= 0)
                        d.Colours[k * cc + colourMap[j]] = c[k * nc + j] & 0xFFFFFF;
                for (int j = 0; j < nf; j++)
                    if (floatMap[j] >= 0)
                        d.Floats[k * fc + floatMap[j]] = f[k * nf + j];
                for (int j = 0; j < ni; j++)
                    if (intMap[j] >= 0)
                        d.Ints[k * ic + intMap[j]] = i[k * ni + j];
            }
            for (int k = 0; k < n; k++)
                WorldFog.DeriveKey(d.Colours, k * cc, d.Floats, k * fc, d.Fog, k * WorldFog.DerivedCount);
            return d;
        }

        /// <summary>The keys around half-minute t and the share f of the way from a to b: the key at t (a = b, f 0); else the
        /// last key before t and the first after it, across midnight when t lies before the first key or after the last
        /// (exactly the host's LightCatalog::sample()); `clamp` (a debug arm): before the first key the first, after the last
        /// the last, f 0.</summary>
        public void Bracket(int t, bool clamp, out int a, out int b, out double f)
        {
            int n = Times.Length;
            a = b = -1;
            for (int k = 0; k < n; k++)
                if (Times[k] == t)
                    a = b = k;
            double ta = 0.0, tb = 0.0;
            if (a < 0)
            {
                if (clamp && t < Times[0])
                    a = b = 0;
                else if (clamp && t > Times[n - 1])
                    a = b = n - 1;
                else
                {
                    int before = -1, after = -1;
                    for (int k = 0; k < n; k++)
                    {
                        if (Times[k] < t)
                            before = k;
                        else if (after < 0)
                            after = k;
                    }
                    a = before >= 0 ? before : n - 1;
                    ta = before >= 0 ? Times[before] : Times[n - 1] - Day;
                    b = after >= 0 ? after : 0;
                    tb = after >= 0 ? Times[after] : Times[0] + Day;
                }
            }
            f = (a == b || tb <= ta) ? 0.0 : (t - ta) / (tb - ta);
        }

        /// <summary>One channel of a colour between two keys, as the host rounds it: floor(a + (b - a) f + 0.5), 0..255.</summary>
        public static int Channel(int from, int to, int shift, double f)
        {
            return Round((from >> shift) & 0xFF, (to >> shift) & 0xFF, f);
        }

        /// <summary>A byte between two keys' bytes, as the host rounds it: floor(a + (b - a) f + 0.5), 0..255 (Channel's arithmetic,
        /// also for the derived fog colours).</summary>
        public static int Round(double va, double vb, double f)
        {
            int v = (int)Math.Floor(va + (vb - va) * f + 0.5);
            return Math.Min(255, Math.Max(0, v));
        }

        /// <summary>The day at half-minute t: `rgb` (ColourCount x 3 bytes, r g b per column), `floats` (FloatCount, linear),
        /// `ints` (IntCount, the earlier key's). Arrays at least that long; nothing is allocated.</summary>
        public void Evaluate(int t, bool clamp, int[] rgb, double[] floats, long[] ints)
        {
            Evaluate(t, clamp, rgb, floats, ints, null);
        }

        /// <summary>Evaluate with the derived fog channels too (Stage 10 step 7): `fog` (WorldFog.DerivedCount) blended with the
        /// same keys and share, channels 0-7 linearly and the colour bytes (WorldFog.DerivedColours on) with the host's rounding
        /// (Round). Any output may be null.</summary>
        public void Evaluate(int t, bool clamp, int[] rgb, double[] floats, long[] ints, double[] fog)
        {
            int a, b;
            double f;
            Bracket(t, clamp, out a, out b, out f);
            int cc = WorldLightColumns.ColourCount, fc = WorldLightColumns.FloatCount, ic = WorldLightColumns.IntCount;
            if (rgb != null)
                for (int j = 0; j < cc; j++)
                {
                    int ca = Colours[a * cc + j], cb = Colours[b * cc + j];
                    rgb[3 * j] = Channel(ca, cb, 16, f);
                    rgb[3 * j + 1] = Channel(ca, cb, 8, f);
                    rgb[3 * j + 2] = Channel(ca, cb, 0, f);
                }
            if (floats != null)
                for (int j = 0; j < fc; j++)
                {
                    double x = Floats[a * fc + j], y = Floats[b * fc + j];
                    floats[j] = x + (y - x) * f;
                }
            if (ints != null)
                for (int j = 0; j < ic; j++)
                    ints[j] = Ints[a * ic + j];
            if (fog != null)
            {
                int dc = WorldFog.DerivedCount;
                for (int j = 0; j < dc; j++)
                {
                    double x = Fog[a * dc + j], y = Fog[b * dc + j];
                    fog[j] = j < WorldFog.DerivedColours ? x + (y - x) * f : Round(x, y, f);
                }
            }
        }

        /// <summary>One colour column at half-minute t (r, g, b), as Evaluate gives it.</summary>
        public void Colour(int t, int column, bool clamp, out int r, out int g, out int b)
        {
            int ka, kb;
            double f;
            Bracket(t, clamp, out ka, out kb, out f);
            int cc = WorldLightColumns.ColourCount;
            int ca = Colours[ka * cc + column], cb = Colours[kb * cc + column];
            r = Channel(ca, cb, 16, f);
            g = Channel(ca, cb, 8, f);
            b = Channel(ca, cb, 0, f);
        }
    }

    /// <summary>One ZoneLight polygon of the map (the host's order is kept).</summary>
    public sealed class WorldLightZone
    {
        public int Zone, Light, Priority, Conditional;   // Priority: ZoneLight.TransitionType (public name; 0 where absent)
        public double ZMin, ZMax;
        public double[] Poly;                             // x0, y0, x1, y1, ... (game X north, game Y west, yards)
        public double MinX, MaxX, MinY, MaxY;

        public bool Contains(double x, double y) { return LightGeometry.Contains(Poly, MinX, MaxX, MinY, MaxY, x, y); }
    }

    /// <summary>One point light of the map (Stage 09's set).</summary>
    public sealed class WorldLightPoint
    {
        public int Light;
        public double X, Y, Z, Start, End;

        public double Weight(double d) { return LightGeometry.PointWeight(Start, End, d); }
    }

    /// <summary>One LightParams row as the host sends it.</summary>
    public sealed class WorldLightParams
    {
        public int Id;
        public int Day = -1;                  // index into the set's days, -1: no keys
        public double Glow;
        public int Skybox;                    // LightSkyboxID (0: none)
        public long Flags;                    // 0x100: a sun-position override, 0x200: a light-direction override (INFERRED (public))
        public int HighlightSky;
        public readonly double[] OverrideSun = new double[3];
        public double SunPolar, SunAzimuth;
    }

    /// <summary>Which debug arm the selection and the day take (the defaults are Stage 09's; see the file header).</summary>
    public enum WorldLightPointOrder { WeakestFirst = 0, StrongestFirst = 1, FarthestFirst = 2 }

    public struct WorldLightRules
    {
        public WorldLightPointOrder PointOrder;   // WeakestFirst: Stage 09's
        public bool Distance2D;                   // false: 3D distance, Stage 09's
        public bool Clamp;                        // false: the day wraps past midnight

        public bool IsDefault { get { return PointOrder == WorldLightPointOrder.WeakestFirst && !Distance2D && !Clamp; } }

        public override string ToString()
        {
            return string.Format("points {0}, {1} distance, the day {2}", PointOrder == WorldLightPointOrder.WeakestFirst ? "weakest first"
                                                                          : PointOrder == WorldLightPointOrder.StrongestFirst ? "strongest first"
                                                                          : "farthest first",
                                 Distance2D ? "2D" : "3D", Clamp ? "clamped at its ends" : "wrapped past midnight");
        }
    }

    /// <summary>What Select found: the base light and the point lights applied over it, in their order.</summary>
    public sealed class WorldLightSelection
    {
        public const string WhyNoLight = "no zone contains the point and the map has no default light";
        public const string WhyNoKeys = "the base light's LightParams has no keys";

        public bool Valid;
        public string Why = "";
        public int Light;                         // the base light, 0 = none
        public int Zone = -1;                     // its zone (ZoneLight id), 0 = the map's default light, -1 = none
        public int Params;                        // the base light's LightParams (0 when none)
        public int Slot;                          // the slot asked for
        public bool SlotFellBack;                 // the base light's slot had no keys: slot 0's were taken
        public WorldLightDay Day;                 // the base light's day (null when invalid)
        public WorldLightParams ParamsRow;
        /// <summary>The point lights applied, in their order: weight W, light id, their LightParams (with its day).</summary>
        public readonly List<LightGeometry.Weighted<WorldLightParams>> Points = new List<LightGeometry.Weighted<WorldLightParams>>(8);

        public void Reset()
        {
            Valid = false;
            Why = "";
            Light = Params = Slot = 0;
            Zone = -1;
            SlotFellBack = false;
            Day = null;
            ParamsRow = null;
            Points.Clear();
        }
    }

    /// <summary>The host's "worldLight" object for one map (see the file header). Filled through the Add methods (days before
    /// the params that name them); entries they refuse are counted, never guessed at.</summary>
    public sealed class WorldLightSet
    {
        public string State = "", Detail = "", Product = "", Build = "", Branch = "", LightDataLayout = "", DefaultFrom = "";
        public int Map, DefaultLight;
        /// <summary>Map.TimeOfDayOverride as sent (minutes); -1 none (also when the object did not carry it).</summary>
        public int TimeOverride = -1;
        /// <summary>The override in minutes when it lies within the day, else -1: none (the Classic layouts' 16-bit -1 reads
        /// 65535 where a host does not extend its sign).</summary>
        public int TimeOverrideMinutes { get { return TimeOverride >= 0 && TimeOverride < 1440 ? TimeOverride : -1; } }
        public long[] MapFlags = new long[3];
        public readonly List<WorldLightZone> Zones = new List<WorldLightZone>();
        public readonly List<WorldLightPoint> Points = new List<WorldLightPoint>();
        readonly Dictionary<int, int[]> slots = new Dictionary<int, int[]>();
        readonly Dictionary<int, WorldLightParams> parameters = new Dictionary<int, WorldLightParams>();
        readonly List<WorldLightDay> days = new List<WorldLightDay>();
        int[] colourMap = new int[0], floatMap = new int[0], intMap = new int[0];
        /// <summary>The columns as sent, and the player's columns this client's layout lacks or the object did not send.</summary>
        public string[] ColourColumns = new string[0], FloatColumns = new string[0], IntColumns = new string[0], AbsentColumns = new string[0];
        readonly bool[] colourPresent = new bool[WorldLightColumns.ColourCount], floatPresent = new bool[WorldLightColumns.FloatCount],
                        intPresent = new bool[WorldLightColumns.IntCount];
        /// <summary>Sent columns the player does not know (ignored).</summary>
        public int UnknownColumns;
        public int DroppedZones, DroppedPoints, DroppedLights, DroppedParams, DroppedDays;
        /// <summary>Why the first refused day was refused ("" when none was).</summary>
        public string FirstDroppedDay = "";

        public int LightCount { get { return slots.Count; } }
        public int ParamsCount { get { return parameters.Count; } }
        public int DayCount { get { return days.Count; } }
        public WorldLightDay DayAt(int index) { return index >= 0 && index < days.Count ? days[index] : null; }

        /// <summary>The sent column order (before any day). A column in `absent` is the layout's lack: it is read as not present
        /// (its group's validity, WorldLightSample).</summary>
        public void SetColumns(string[] colours, string[] floats, string[] ints, string[] absent)
        {
            ColourColumns = colours ?? new string[0];
            FloatColumns = floats ?? new string[0];
            IntColumns = ints ?? new string[0];
            AbsentColumns = absent ?? new string[0];
            colourMap = WorldLightColumns.Map(ColourColumns, WorldLightColumns.Colours);
            floatMap = WorldLightColumns.Map(FloatColumns, WorldLightColumns.Floats);
            intMap = WorldLightColumns.Map(IntColumns, WorldLightColumns.Ints);
            UnknownColumns = 0;
            Array.Clear(colourPresent, 0, colourPresent.Length);
            Array.Clear(floatPresent, 0, floatPresent.Length);
            Array.Clear(intPresent, 0, intPresent.Length);
            Mark(colourMap, ColourColumns, colourPresent);
            Mark(floatMap, FloatColumns, floatPresent);
            Mark(intMap, IntColumns, intPresent);
        }

        void Mark(int[] map, string[] sent, bool[] present)
        {
            for (int k = 0; k < map.Length; k++)
            {
                if (map[k] < 0)
                {
                    UnknownColumns++;
                    continue;
                }
                if (Array.IndexOf(AbsentColumns, sent[k]) < 0)
                    present[map[k]] = true;
            }
        }

        /// <summary>Is the column sent and not absent in this client's layout?</summary>
        public bool ColourPresent(int column) { return colourPresent[column]; }
        public bool FloatPresent(int column) { return floatPresent[column]; }
        public bool IntPresent(int column) { return intPresent[column]; }

        /// <summary>Appends a zone (tried in the order added); false, and counted dropped, when its polygon has fewer than 3
        /// points or an odd coordinate count, or a value is not finite.</summary>
        public bool AddZone(int zone, int light, int priority, double zmin, double zmax, int conditional, double[] poly)
        {
            if (!(LightGeometry.Finite(zmin) && LightGeometry.Finite(zmax) && LightGeometry.ValidPolygon(poly)))
            {
                DroppedZones++;
                return false;
            }
            var z = new WorldLightZone { Zone = zone, Light = light, Priority = priority, Conditional = conditional, ZMin = zmin, ZMax = zmax,
                                         Poly = (double[])poly.Clone() };
            LightGeometry.Bounds(poly, out z.MinX, out z.MaxX, out z.MinY, out z.MaxY);
            Zones.Add(z);
            return true;
        }

        /// <summary>Appends a point light; false, and counted dropped, when a value is not finite.</summary>
        public bool AddPoint(int light, double x, double y, double z, double start, double end)
        {
            if (!LightGeometry.Finite(x) || !LightGeometry.Finite(y) || !LightGeometry.Finite(z) || !LightGeometry.Finite(start) ||
                !LightGeometry.Finite(end))
            {
                DroppedPoints++;
                return false;
            }
            Points.Add(new WorldLightPoint { Light = light, X = x, Y = y, Z = z, Start = start, End = end });
            return true;
        }

        /// <summary>A Light's eight LightParams ids (a later entry for the same light replaces the earlier one); false, and
        /// counted dropped, when it carries fewer than 8.</summary>
        public bool AddLight(int light, int[] lightParams)
        {
            if (lightParams == null || lightParams.Length < 8)
            {
                DroppedLights++;
                return false;
            }
            var s = new int[8];
            Array.Copy(lightParams, s, 8);
            slots[light] = s;
            return true;
        }

        /// <summary>A day in the sent column order (SetColumns first); its index, or -1 (counted dropped, with why) when
        /// WorldLightDay.From refuses it. A refused day keeps its index -- the params name days by position -- as no keys.</summary>
        public int AddDay(int[] times, int[] c, double[] f, long[] i)
        {
            string why;
            WorldLightDay d = WorldLightDay.From(times, c, f, i, colourMap, floatMap, intMap, out why);
            days.Add(d);
            if (d == null)
            {
                DroppedDays++;
                if (FirstDroppedDay.Length == 0)
                    FirstDroppedDay = "day " + (days.Count - 1) + ": " + why;
                return -1;
            }
            return days.Count - 1;
        }

        /// <summary>A LightParams row (a later one for the same id replaces it). Its day is an index into the days added so
        /// far, -1 for none; an index past them (or a refused day) is kept as -1 and the row counted dropped.</summary>
        public bool AddParams(int id, int day, double glow, int skybox, long flags, int highlightSky, double[] overrideSun, double sunPolar,
                              double sunAzimuth)
        {
            var p = new WorldLightParams { Id = id, Day = day, Glow = LightGeometry.Finite(glow) ? glow : 0.0, Skybox = skybox, Flags = flags,
                                           HighlightSky = highlightSky, SunPolar = sunPolar, SunAzimuth = sunAzimuth };
            if (overrideSun != null)
                for (int k = 0; k < 3 && k < overrideSun.Length; k++)
                    p.OverrideSun[k] = overrideSun[k];
            bool ok = day == -1 || (day >= 0 && day < days.Count && days[day] != null);
            if (!ok)
            {
                p.Day = -1;
                DroppedParams++;
            }
            parameters[id] = p;
            return ok;
        }

        public bool TryGetSlots(int light, out int[] lightParams) { return slots.TryGetValue(light, out lightParams); }
        public bool TryGetParams(int id, out WorldLightParams p) { return parameters.TryGetValue(id, out p); }

        /// <summary>A light's LightParams of `slot` with its day: the slot's, else (a slot other than 0 without keys) slot 0's
        /// (fellBack); null when neither has keys.</summary>
        public WorldLightParams ParamsOf(int light, int slot, out bool fellBack)
        {
            fellBack = false;
            int[] s;
            if (!slots.TryGetValue(light, out s))
                return null;
            WorldLightParams p = Keyed(s, slot);
            if (p == null && slot != 0)
            {
                p = Keyed(s, 0);
                fellBack = p != null;
            }
            return p;
        }

        WorldLightParams Keyed(int[] s, int slot)
        {
            WorldLightParams p;
            if (slot < 0 || slot >= s.Length || s[slot] == 0 || !parameters.TryGetValue(s[slot], out p) || p.Day < 0 || DayAt(p.Day) == null)
                return null;
            return p;
        }

        /// <summary>The light at game point (x north, y west, z up), yards, for LightParams slot `slot` (see the file header);
        /// into `sel`, without allocating (one thread at a time).</summary>
        public void Select(double x, double y, double z, int slot, WorldLightRules rules, WorldLightSelection sel)
        {
            if (sel == null)
                throw new ArgumentNullException("sel");
            sel.Reset();
            sel.Slot = slot;
            WorldLightZone hit = null;
            foreach (WorldLightZone zone in Zones)
                if (z >= zone.ZMin && z <= zone.ZMax && zone.Contains(x, y))
                {
                    hit = zone;
                    break;
                }
            if (hit != null)
            {
                sel.Zone = hit.Zone;
                sel.Light = hit.Light;
            }
            else if (DefaultLight > 0)
            {
                sel.Zone = 0;
                sel.Light = DefaultLight;
            }
            else
            {
                sel.Why = WorldLightSelection.WhyNoLight;
                return;
            }
            bool fellBack;
            WorldLightParams b = ParamsOf(sel.Light, slot, out fellBack);
            if (b == null)
            {
                sel.Why = WorldLightSelection.WhyNoKeys;
                return;
            }
            sel.Valid = true;
            sel.Params = b.Id;
            sel.ParamsRow = b;
            sel.SlotFellBack = fellBack;
            sel.Day = DayAt(b.Day);
            foreach (WorldLightPoint p in Points)
            {
                double dx = x - p.X, dy = y - p.Y, dz = rules.Distance2D ? 0.0 : z - p.Z;
                double d = Math.Sqrt(dx * dx + dy * dy + dz * dz);
                double w = p.Weight(d);
                if (w <= 0.0)
                    continue;
                bool pf;
                WorldLightParams pp = ParamsOf(p.Light, slot, out pf);
                if (pp == null)
                    continue;
                double key = rules.PointOrder == WorldLightPointOrder.StrongestFirst ? -w
                           : rules.PointOrder == WorldLightPointOrder.FarthestFirst ? -d : w;
                LightGeometry.InsertInOrder(sel.Points, key, w, p.Light, pp);
            }
        }

        /// <summary>The object in a line (the log).</summary>
        public string Summary()
        {
            return string.Format(CultureInfo.InvariantCulture,
                                 "map {0} ({1} {2}, {3} branch, LightData {4}): {5} zone(s), {6} point light(s), {7} light(s), {8} LightParams, " +
                                 "{9} day(s), default light {10} (from {11}), time override {12}{13}{14}",
                                 Map, Product, Build, Branch.Length > 0 ? Branch : "-", LightDataLayout.Length > 0 ? LightDataLayout : "-", Zones.Count,
                                 Points.Count, slots.Count, parameters.Count, days.Count, DefaultLight, DefaultFrom.Length > 0 ? DefaultFrom : "-",
                                 TimeOverrideMinutes >= 0 ? TimeOverrideMinutes + " min"
                                 : TimeOverride != -1 ? "none (sent " + TimeOverride + ")" : "none",
                                 AbsentColumns.Length > 0 || UnknownColumns > 0
                                     ? string.Format(CultureInfo.InvariantCulture, "; {0} column(s) absent in the layout, {1} unknown", AbsentColumns.Length,
                                                     UnknownColumns)
                                     : "",
                                 DroppedZones + DroppedPoints + DroppedLights + DroppedParams + DroppedDays > 0
                                     ? string.Format(CultureInfo.InvariantCulture, "; dropped {0} zone(s), {1} point(s), {2} light(s), {3} LightParams, {4} day(s){5}",
                                                     DroppedZones, DroppedPoints, DroppedLights, DroppedParams, DroppedDays,
                                                     FirstDroppedDay.Length > 0 ? " (" + FirstDroppedDay + ")" : "")
                                     : "");
        }
    }
}
