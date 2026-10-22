// WorldLightState.cs
//
// THE OUTDOOR LIGHT STATE (Stage 10, docs/world-viewer/10-light-sky-fog.md 6.4, 6.13): the clock, one evaluated sample of the
// light at a point and time, the ease over frames and the switches. Pure C#, no UnityEngine: WmvWorldLight drives it once a
// rendered frame for the main camera in the world view; Tests/WorldLightTests.cs checks it.
//
//   the clock     (WorldClock) the game's time of day in seconds, PINNED by default at 12:00:00 (the owner's D13) or RUNNING
//                 at real speed (1 s a second; the sandbox clock does, PROVEN_BYTES: the client's log); a map's
//                 TimeOfDayOverride (minutes, INFERRED) wins while it is set. Independent of -wmvAnimTime. The light's
//                 bands step per half-minute (t = floor(seconds / 30): t = 2 (60 h + m) at h:m:00, INFERRED) while the
//                 sun and the light direction move continuously.
//   the sample    (WorldLightSample, WorldLightEvaluator) the selection (WorldLightSet.Select) and the base light's day at the
//                 half-minute -- every colour as the host rounds it (byte / 255 in the sample), every float linear, the
//                 integers the earlier key's -- then each point light's day lerped over it by its weight (colours, floats
//                 and Glow; the integers stay the base light's: how the client blends them is UNKNOWN). Named channel groups,
//                 each VALID when the base light has keys and the client's layout carries one of its columns.
//   the ease      (WorldLightEase) Stage 09's (LiquidLightEase's constants, generalised to every channel): 1 - exp(-dt /
//                 0.25 s) a frame; snapped on the first sample, another object, a change of validity, PINNED CLOCKS -- in
//                 Stage 09's sense, -wmvAnimTime (WmvModelBuilder.Debug_.AnimTime >= 0), as LiquidLightFrame passes it to
//                 the water; the world clock's own pinning does not snap --, a move of more than 60 yd and a clock jump (the
//                 host set the time or the clock, or the map's override came or went). So the outdoor light and the water
//                 ease alike across a zone or point-light boundary (plan 6.1 rule 2).
//   the switches  (WorldLightSwitches) WMV_LIGHT=off (hard off: nothing is evaluated or set, Stage 09 exactly; read once),
//                 WMV_LIGHT=on or -wmvWorldLight=on (opt in: the default is off until the owner accepts the stage, D14),
//                 -wmvWorldLight=off, -wmvGameTime=HH:MM[:SS] (the pinned time), -wmvGameFarclip=N (yards, default 7000),
//                 the debug arms -wmvLightRule=weakest|strongest|farthest[+2d] and -wmvLightWrap=wrap|clamp; Stage 10 steps 4
//                 and 5: -wmvSky=off (no sky, the rest lit), -wmvSkyTable=a|b (the dome's public ring table, SkyDome.cs; a by
//                 default), -wmvSkyEndFog=off (ring 5 plain SkyFogColor), -wmvTerrainLight=off (the sky kept, the terrain as
//                 Stage 09 draws it) and -wmvTerrainVariant=hemisphere|flat (the client's flat class, for the record only;
//                 TerrainLight.cs).

using System;
using System.Collections.Generic;
using System.Globalization;

namespace Wmv.Wow.World
{
    /// <summary>The game's time of day (see the file header).</summary>
    public sealed class WorldClock
    {
        public const double Day = 86400.0;
        public const double Noon = 43200.0;
        public const string SourcePinned = "pinned", SourceRunning = "running", SourceMap = "map";

        double seconds = Noon;
        /// <summary>The clock's own time (seconds into the day, [0, Day)).</summary>
        public double Seconds { get { return seconds; } set { seconds = Wrap(value); } }
        public bool Running;
        /// <summary>The map's TimeOfDayOverride in minutes, -1 none: it wins while set.</summary>
        public int MapOverrideMinutes = -1;

        public static double Wrap(double s)
        {
            if (!LightGeometry.Finite(s))
                return Noon;
            s %= Day;
            return s < 0.0 ? s + Day : s;
        }

        /// <summary>The map's override applies: minutes within the day.</summary>
        public bool Overridden { get { return MapOverrideMinutes >= 0 && MapOverrideMinutes < 1440; } }
        public string Source { get { return Overridden ? SourceMap : Running ? SourceRunning : SourcePinned; } }
        /// <summary>The time the light takes (seconds).</summary>
        public double Effective { get { return Overridden ? MapOverrideMinutes * 60.0 : seconds; } }
        public int HalfMinute { get { return HalfMinuteOf(Effective); } }
        /// <summary>The clock does not run (the override pins it too). Not the ease's "pinned": that is -wmvAnimTime's (see the
        /// file header), so a pinned world clock still eases across a light boundary.</summary>
        public bool Pinned { get { return Overridden || !Running; } }

        /// <summary>The map's override from its outdoor light object (none without one); true when the time the light takes
        /// jumped with it (an override came, went or changed).</summary>
        public bool UseMap(WorldLightSet set)
        {
            int m = set != null ? set.TimeOverrideMinutes : -1;
            bool was = Overridden;
            int before = MapOverrideMinutes;
            MapOverrideMinutes = m;
            return was != Overridden || (Overridden && before != m);
        }

        /// <summary>One frame of `dt` seconds: a running clock moves on (wrapping at midnight).</summary>
        public void Advance(double dt)
        {
            if (Running && dt > 0.0 && LightGeometry.Finite(dt))
                seconds = Wrap(seconds + dt);
        }

        /// <summary>The half-minute of a time of day: floor(seconds / 30), within [0, 2880).</summary>
        public static int HalfMinuteOf(double s)
        {
            int t = (int)Math.Floor(Wrap(s) / 30.0);
            return Math.Min(WorldLightDay.Day - 1, Math.Max(0, t));
        }

        /// <summary>"HH:MM" or "HH:MM:SS" (hours 0-23, minutes and seconds 0-59) as seconds; false for anything else.</summary>
        public static bool TryParse(string text, out double s)
        {
            s = Noon;
            if (string.IsNullOrEmpty(text))
                return false;
            string[] parts = text.Split(':');
            if (parts.Length != 2 && parts.Length != 3)
                return false;
            int h, m, sec = 0;
            if (!Part(parts[0], 23, out h) || !Part(parts[1], 59, out m) || (parts.Length == 3 && !Part(parts[2], 59, out sec)))
                return false;
            s = h * 3600.0 + m * 60.0 + sec;
            return true;
        }

        static bool Part(string p, int max, out int v)
        {
            v = 0;
            if (p.Length < 1 || p.Length > 2)
                return false;
            foreach (char c in p)
                if (c < '0' || c > '9')
                    return false;
            v = int.Parse(p, CultureInfo.InvariantCulture);
            return v <= max;
        }

        /// <summary>"HH:MM:SS" (whole seconds, rounded down).</summary>
        public static string Format(double s)
        {
            int t = (int)Math.Floor(Wrap(s));
            return string.Format(CultureInfo.InvariantCulture, "{0:00}:{1:00}:{2:00}", t / 3600, t / 60 % 60, t % 60);
        }
    }

    /// <summary>The outdoor light at one point and time (see the file header). Colours are authored 0..1 (byte / 255).</summary>
    public sealed class WorldLightSample
    {
        /// <summary>The channel layout: every colour column (WorldLightColumns.Colours) as r, g, b; then every float column;
        /// then the LightParams' Glow.</summary>
        public static readonly int FloatBase = 3 * WorldLightColumns.ColourCount;
        public static readonly int Glow = FloatBase + WorldLightColumns.FloatCount;
        public static readonly int Count = Glow + 1;
        public static int ColourAt(int column) { return 3 * column; }
        public static int FloatAt(int column) { return FloatBase + column; }

        /// <summary>The channel groups (Groups, a mask).</summary>
        public const int GroupSky = 1, GroupAmbient = 2, GroupDirect = 4, GroupSun = 8, GroupFog = 16, GroupFogColours = 32, GroupClouds = 64,
                         GroupWater = 128, GroupShadow = 256, GroupGrading = 512, GroupGlow = 1024;
        public static readonly string[] GroupNames = { "sky", "ambient", "direct", "sun", "fog", "fog colours", "clouds", "water", "shadow",
                                                       "grading", "glow" };

        /// <summary>The group of a colour column.</summary>
        public static int GroupOfColour(int column)
        {
            switch (column)
            {
                case WorldLightColumns.SkyTopColor: case WorldLightColumns.SkyMiddleColor: case WorldLightColumns.SkyBand1Color:
                case WorldLightColumns.SkyBand2Color: case WorldLightColumns.SkySmogColor: case WorldLightColumns.SkyFogColor:
                    return GroupSky;
                case WorldLightColumns.AmbientColor: case WorldLightColumns.HorizonAmbientColor: case WorldLightColumns.GroundAmbientColor:
                    return GroupAmbient;
                case WorldLightColumns.DirectColor:
                    return GroupDirect;
                case WorldLightColumns.SunColor:
                    return GroupSun;
                case WorldLightColumns.CloudSunColor: case WorldLightColumns.CloudEmissiveColor: case WorldLightColumns.CloudLayer1AmbientColor:
                case WorldLightColumns.CloudLayer2AmbientColor:
                    return GroupClouds;
                case WorldLightColumns.OceanCloseColor: case WorldLightColumns.OceanFarColor: case WorldLightColumns.RiverCloseColor:
                case WorldLightColumns.RiverFarColor:
                    return GroupWater;
                case WorldLightColumns.ShadowOpacity:
                    return GroupShadow;
                default:
                    return GroupFogColours;   // EndFog, SunFog, FogHeight, EndFogHeight
            }
        }

        /// <summary>The group of a float column.</summary>
        public static int GroupOfFloat(int column) { return column == WorldLightColumns.CloudDensity ? GroupClouds : GroupFog; }

        public static string DescribeGroups(int groups)
        {
            var names = new List<string>();
            for (int k = 0; k < GroupNames.Length; k++)
                if ((groups & (1 << k)) != 0)
                    names.Add(GroupNames[k]);
            return names.Count > 0 ? string.Join(" ", names.ToArray()) : "none";
        }

        public bool Valid;
        public string Why = "";
        public int Light, Params, Slot, Points;
        public int Zone = -1;
        public bool SlotFellBack;
        public int Groups;                        // the valid channel groups
        public int HalfMinute;
        public double Seconds;                    // the time the light took (seconds into the day)
        public int Skybox, HighlightSky;          // the base light's LightParams
        public long Flags;
        public readonly double[] V = new double[Count];
        /// <summary>The base light's integer columns at the half-minute (not blended).</summary>
        public readonly long[] Ints = new long[WorldLightColumns.IntCount];
        /// <summary>The base light's colours at the half-minute, bytes (r, g, b per column): what the host's sample() gives.</summary>
        public readonly int[] BaseRgb = new int[3 * WorldLightColumns.ColourCount];

        public void Reset()
        {
            Valid = false;
            Why = "";
            Light = Params = Slot = Points = 0;
            Zone = -1;
            SlotFellBack = false;
            Groups = 0;
            Skybox = HighlightSky = 0;
            Flags = 0;
            Array.Clear(V, 0, V.Length);
            Array.Clear(Ints, 0, Ints.Length);
            Array.Clear(BaseRgb, 0, BaseRgb.Length);
        }

        /// <summary>A colour column (0..1).</summary>
        public void Colour(int column, out double r, out double g, out double b)
        {
            int at = ColourAt(column);
            r = V[at];
            g = V[at + 1];
            b = V[at + 2];
        }

        public double Float(int column) { return V[FloatAt(column)]; }
    }

    /// <summary>Evaluates the sample of a set at a point and time (see the file header). One instance per thread; nothing is
    /// allocated per call.</summary>
    public sealed class WorldLightEvaluator
    {
        readonly WorldLightSelection selection = new WorldLightSelection();
        readonly int[] rgb = new int[3 * WorldLightColumns.ColourCount];
        readonly double[] floats = new double[WorldLightColumns.FloatCount];

        /// <summary>The last evaluation's selection (its point lights in their order).</summary>
        public WorldLightSelection Selection { get { return selection; } }

        public void Evaluate(WorldLightSet set, double x, double y, double z, double seconds, int slot, WorldLightRules rules, WorldLightSample s)
        {
            if (s == null)
                throw new ArgumentNullException("s");
            s.Reset();
            s.Seconds = WorldClock.Wrap(seconds);
            s.HalfMinute = WorldClock.HalfMinuteOf(seconds);
            s.Slot = slot;
            if (set == null)
            {
                s.Why = "no outdoor light object";
                return;
            }
            set.Select(x, y, z, slot, rules, selection);
            s.Light = selection.Light;
            s.Zone = selection.Zone;
            s.Why = selection.Why;
            if (!selection.Valid)
                return;
            s.Valid = true;
            s.Params = selection.Params;
            s.SlotFellBack = selection.SlotFellBack;
            WorldLightParams b = selection.ParamsRow;
            s.Skybox = b.Skybox;
            s.Flags = b.Flags;
            s.HighlightSky = b.HighlightSky;
            int t = s.HalfMinute;
            selection.Day.Evaluate(t, rules.Clamp, s.BaseRgb, floats, s.Ints);
            for (int k = 0; k < s.BaseRgb.Length; k++)
                s.V[k] = s.BaseRgb[k] / 255.0;
            for (int k = 0; k < floats.Length; k++)
                s.V[WorldLightSample.FloatBase + k] = floats[k];
            s.V[WorldLightSample.Glow] = b.Glow;
            foreach (LightGeometry.Weighted<WorldLightParams> p in selection.Points)
            {
                WorldLightDay d = set.DayAt(p.Item.Day);
                d.Evaluate(t, rules.Clamp, rgb, floats, null);
                double w = p.W;
                for (int k = 0; k < rgb.Length; k++)
                    s.V[k] += (rgb[k] / 255.0 - s.V[k]) * w;
                for (int k = 0; k < floats.Length; k++)
                    s.V[WorldLightSample.FloatBase + k] += (floats[k] - s.V[WorldLightSample.FloatBase + k]) * w;
                s.V[WorldLightSample.Glow] += (p.Item.Glow - s.V[WorldLightSample.Glow]) * w;
            }
            s.Points = selection.Points.Count;
            int groups = WorldLightSample.GroupGlow;
            for (int c = 0; c < WorldLightColumns.ColourCount; c++)
                if (set.ColourPresent(c))
                    groups |= WorldLightSample.GroupOfColour(c);
            for (int f = 0; f < WorldLightColumns.FloatCount; f++)
                if (set.FloatPresent(f))
                    groups |= WorldLightSample.GroupOfFloat(f);
            for (int i = 0; i < WorldLightColumns.IntCount; i++)
                if (set.IntPresent(i))
                    groups |= WorldLightSample.GroupGrading;
            s.Groups = groups;
        }
    }

    /// <summary>
    /// The eased channels the frame's globals are given (see the file header): Step once a rendered frame with the frame's
    /// sample. Stage 09's ease (LiquidLightEase: its time constant, jump distance and factor) over every channel, and a clock
    /// jump snaps too.
    /// </summary>
    public sealed class WorldLightEase
    {
        public const string SnapClock = "clock";

        public bool Has, Valid;
        public object Source;
        public double X, Y, Z;
        public int Light, Zone = -1, Params;
        public readonly double[] V = new double[WorldLightSample.Count];

        /// <summary>The snap reason for a sample (null: it eases): LiquidLightEase's reasons, then "clock".</summary>
        public string SnapReason(object source, bool valid, double x, double y, double z, bool pinned, bool clockJumped)
        {
            if (!Has)
                return LiquidLightEase.SnapFirst;
            if (!ReferenceEquals(source, Source))
                return LiquidLightEase.SnapLights;
            if (valid != Valid)
                return LiquidLightEase.SnapValidity;
            if (pinned)
                return LiquidLightEase.SnapPinned;
            if (clockJumped)
                return SnapClock;
            double dx = x - X, dy = y - Y, dz = z - Z;
            if (dx * dx + dy * dy + dz * dz > LiquidLightEase.SnapDistance * LiquidLightEase.SnapDistance)
                return LiquidLightEase.SnapJump;
            return null;
        }

        /// <summary>One frame: null when the channels eased, the snap reason when they snapped, or LiquidLightEase.Invalid (the
        /// channels kept, unused: nothing to show).</summary>
        public string Step(WorldLightSample target, object source, double x, double y, double z, double dt, bool pinned, bool clockJumped)
        {
            if (target == null)
                throw new ArgumentNullException("target");
            string snap = SnapReason(source, target.Valid, x, y, z, pinned, clockJumped);
            Has = true;
            Source = source;
            Valid = target.Valid;
            X = x;
            Y = y;
            Z = z;
            Light = target.Light;
            Zone = target.Zone;
            Params = target.Params;
            if (!target.Valid)
                return LiquidLightEase.Invalid;
            double f = snap != null ? 1.0 : LiquidLightEase.Factor(dt);
            for (int k = 0; k < V.Length; k++)
                V[k] = snap != null ? target.V[k] : V[k] + (target.V[k] - V[k]) * f;
            return snap;
        }

        public void Reset()
        {
            Has = false;
            Valid = false;
            Source = null;
            Light = Params = 0;
            Zone = -1;
        }
    }

    /// <summary>The outdoor light's switches (see the file header), parsed once from the command line, WMV_DEBUG's tokens and
    /// WMV_LIGHT. A value refused is one of the Notes, never guessed at.</summary>
    public sealed class WorldLightSwitches
    {
        public bool HardOff;                              // WMV_LIGHT=off
        public bool On;                                   // opted in (WMV_LIGHT=on, -wmvWorldLight=on)
        public double TimeSeconds = WorldClock.Noon;      // -wmvGameTime (pinned)
        public bool TimeGiven;
        public double FarClip = WorldLightModel.DefaultFarClip;
        public WorldLightRules Rules;
        public bool Sky = true;                           // -wmvSky=off: no sky
        public bool SkyTableB;                            // -wmvSkyTable=b: the older public ring table
        public bool SkyEndFog = true;                     // -wmvSkyEndFog=off: ring 5 plain SkyFogColor
        public bool TerrainLight = true;                  // -wmvTerrainLight=off: the terrain as Stage 09 draws it
        public bool TerrainFlat;                          // -wmvTerrainVariant=flat: the flat class (for the record)
        public readonly List<string> Notes = new List<string>();

        /// <summary>The defaults of the sky and terrain switches (steps 4 and 5)?</summary>
        public bool DrawDefaults { get { return Sky && !SkyTableB && SkyEndFog && TerrainLight && !TerrainFlat; } }

        static bool OnOff(string a, string name, ref bool value, List<string> notes)
        {
            if (!a.StartsWith(name + "=", StringComparison.Ordinal))
                return false;
            string v = a.Substring(name.Length + 1);
            if (v == "on")
                value = true;
            else if (v == "off")
                value = false;
            else
                notes.Add(a + " is not " + name + "=on or =off -- ignored");
            return true;
        }

        public static WorldLightSwitches Parse(IEnumerable<string> args, string envLight)
        {
            var w = new WorldLightSwitches();
            if (!string.IsNullOrEmpty(envLight))
            {
                if (string.Equals(envLight, "off", StringComparison.OrdinalIgnoreCase))
                    w.HardOff = true;
                else if (string.Equals(envLight, "on", StringComparison.OrdinalIgnoreCase))
                    w.On = true;
                else
                    w.Notes.Add("WMV_LIGHT=" + envLight + " is not on or off -- ignored");
            }
            bool cmdOn = false, cmdOff = false;
            if (args != null)
                foreach (string a in args)
                {
                    if (a == null)
                        continue;
                    if (a == "-wmvWorldLight=on")
                        cmdOn = true;
                    else if (a == "-wmvWorldLight=off")
                        cmdOff = true;
                    else if (a.StartsWith("-wmvWorldLight=", StringComparison.Ordinal))
                        w.Notes.Add(a + " is not -wmvWorldLight=on or =off -- ignored");
                    else if (a.StartsWith("-wmvGameTime=", StringComparison.Ordinal))
                    {
                        double s;
                        string v = a.Substring("-wmvGameTime=".Length);
                        if (WorldClock.TryParse(v, out s))
                        {
                            w.TimeSeconds = s;
                            w.TimeGiven = true;
                        }
                        else
                            w.Notes.Add(a + " is not HH:MM or HH:MM:SS (00:00:00..23:59:59) -- the clock starts at 12:00:00");
                    }
                    else if (a.StartsWith("-wmvGameFarclip=", StringComparison.Ordinal))
                    {
                        double n;
                        string v = a.Substring("-wmvGameFarclip=".Length);
                        if (double.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out n) && LightGeometry.Finite(n) && n > 0.0)
                            w.FarClip = n;
                        else
                            w.Notes.Add(a + " is not a number of yards above 0 -- " + WorldLightModel.DefaultFarClip + " is used");
                    }
                    else if (a.StartsWith("-wmvLightRule=", StringComparison.Ordinal))
                    {
                        string v = a.Substring("-wmvLightRule=".Length);
                        bool twoD = v.EndsWith("+2d", StringComparison.Ordinal);
                        string order = twoD ? v.Substring(0, v.Length - 3) : v;
                        if (order == "weakest" || order == "strongest" || order == "farthest")
                        {
                            w.Rules.PointOrder = order == "strongest" ? WorldLightPointOrder.StrongestFirst
                                               : order == "farthest" ? WorldLightPointOrder.FarthestFirst : WorldLightPointOrder.WeakestFirst;
                            w.Rules.Distance2D = twoD;
                        }
                        else
                            w.Notes.Add(a + " is not weakest|strongest|farthest[+2d] -- Stage 09's rule (weakest first, 3D)");
                    }
                    else if (a == "-wmvLightWrap=clamp")
                        w.Rules.Clamp = true;
                    else if (a == "-wmvLightWrap=wrap")
                        w.Rules.Clamp = false;
                    else if (a.StartsWith("-wmvLightWrap=", StringComparison.Ordinal))
                        w.Notes.Add(a + " is not -wmvLightWrap=wrap or =clamp -- the day wraps");
                    else if (OnOff(a, "-wmvSky", ref w.Sky, w.Notes) || OnOff(a, "-wmvSkyEndFog", ref w.SkyEndFog, w.Notes) ||
                             OnOff(a, "-wmvTerrainLight", ref w.TerrainLight, w.Notes))
                    {
                    }
                    else if (a == "-wmvSkyTable=a" || a == "-wmvSkyTable=b")
                        w.SkyTableB = a == "-wmvSkyTable=b";
                    else if (a.StartsWith("-wmvSkyTable=", StringComparison.Ordinal))
                        w.Notes.Add(a + " is not -wmvSkyTable=a or =b -- table a");
                    else if (a == "-wmvTerrainVariant=hemisphere" || a == "-wmvTerrainVariant=flat")
                        w.TerrainFlat = a == "-wmvTerrainVariant=flat";
                    else if (a.StartsWith("-wmvTerrainVariant=", StringComparison.Ordinal))
                        w.Notes.Add(a + " is not -wmvTerrainVariant=hemisphere or =flat -- the hemisphere variant");
                }
            if (cmdOn && cmdOff)
                w.Notes.Add("-wmvWorldLight=on and =off both given -- off");
            if (cmdOff)
                w.On = false;
            else if (cmdOn)
                w.On = true;
            if (w.HardOff)
                w.On = false;
            return w;
        }

        public string Describe()
        {
            return string.Format(CultureInfo.InvariantCulture, "{0}; clock pinned at {1}{2}; game far clip {3} yd{4}; {5}; {6}{7}",
                                 HardOff ? "WMV_LIGHT=off: hard off (nothing evaluated or set: Stage 09 exactly)"
                                 : On ? "on (opted in)" : "off until the host or a switch turns it on (WMV_LIGHT=on, -wmvWorldLight=on)",
                                 WorldClock.Format(TimeSeconds), TimeGiven ? " (-wmvGameTime)" : "", FarClip,
                                 FarClip != WorldLightModel.DefaultFarClip ? " (-wmvGameFarclip)" : "",
                                 Rules.IsDefault ? "Stage 09's selection" : "DEBUG ARM: " + Rules, DescribeDraw(),
                                 Notes.Count > 0 ? "; " + string.Join("; ", Notes.ToArray()) : "");
        }

        /// <summary>What the sky and the terrain draw (steps 4 and 5).</summary>
        public string DescribeDraw()
        {
            return (Sky ? "sky: the colour dome, table " + (SkyTableB ? "b (-wmvSkyTable=b)" : "a") +
                          (SkyEndFog ? ", ring 5 leaning to EndFogColor" : ", ring 5 plain SkyFogColor (-wmvSkyEndFog=off)")
                        : "sky off (-wmvSky=off)") +
                   (TerrainLight ? "; terrain lit, the " + (TerrainFlat ? "FLAT variant (-wmvTerrainVariant=flat, for the record)" : "hemisphere variant")
                                 : "; terrain as Stage 09 (-wmvTerrainLight=off)");
        }
    }
}
