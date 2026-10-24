// WorldFog.cs
//
// THE CLIENT'S PER-PIXEL FOG (Stage 10 step 7, docs/world-viewer/10-light-sky-fog.md 4.0, 4.6, 6.9, 6.13): the fog tail every
// terrain, M2 and world-model program of the client ends with (layer 0), and the table-to-record mapping that fills its 14-row
// record from the outdoor light state, in pure C# (no UnityEngine). Resources/WmvWorldFog.hlsl evaluates the tail on the lit
// terrain and in the world-lit variants; WmvWorldLight packs the record once a frame (Pack) and uploads it; the lifecycle
// self-test compares the GPU with Apply (the CPU twin); Tests/WorldFogTests.cs checks Apply against the client's own terrain
// and M2 programs re-executed offline, and Pack against the client's keys.
//
//   the tail     (PROVEN_BYTES, executed for the dx_5_0 programs: terrain 2977247 p_1:126-271, the same tail in every M2
//                program, combiners_opaque 2977063 p_1, and world-model program, 5446718 p_1; the dx_6_0 programs the clients
//                bind under DX12 are INFERRED to equal them by structure). R(k) is row k of the record, p the fog position
//                (fragment - camera), raw = |p|, up the program's up row, lit the colour the program computed before fog:
//                  power path (R1.x != 0): vis = min(pow(max((R1.y - raw) R1.z, 0), R1.w), 1), hf = 0      (p_1:236-244)
//                  else  d   = |p - up clamp(up . p, -m, m)|, m = max(R8.w, 0)                         (the pull, p_1:141-163)
//                        D   = max(d - R0.w - R0.x, 0)                                              (p_1:164-166)
//                        u   = 1 - sat((R2.xyz . p + R2.w) R3.w), hf = 1 - sat(cubic R9 (u))          (p_1:171-181)
//                        leg = min(lerp(e^(-R0.z D), e^(-R4.x D), hf), sat((1 - d / R0.y) / 0.7))    (p_1:167-191)
//                        art = the polynomial path over [R12.y, R12.x], cubics R10 and R11 mixed by hf, its own fade
//                              over the last 0.3 of the end ratio                                    (p_1:192-231)
//                        vis = lerp(art, leg, R12.z)                                                (p_1:232-233)
//                  c   = sat((raw - R13.w) / R6.w)                                                  (p_1:246-247)
//                  fc  = lerp(lerp(R3.xyz, R4.yzw, c^3), lerp(R6.xyz, R13.xyz, c), hf)               (p_1:248-253)
//                  x   = sat(sat(p / raw . R8.xyz) - R5.x); where x > 0: fc += (x / (1 - R5.x))^3 R7.x (R5.yzw - fc),
//                        the cube not saturated                                                     (p_1:254-265)
//                  out = (lerp(fc, lit.rgb, vis), lit.a)                                            (p_1:266-268)
//                with cubic c (x) = c.x x^3 + c.y x^2 + c.z x + c.w (the program's order). The fog is the program's last
//                arithmetic, after the light, the lobes and the alpha: no clamp of rgb, no encode, no decode of a fog colour.
//                The literals are the program's: 1/0.7 (p_1:190), 0.3 (p_1:211), 1/0.3 (p_1:225).
//   the holds    one record (layer 0) with layer weight R12.w 1; the second start term R0.w 0 (neutral; its source UNKNOWN);
//                the M2 and world-model strength cb0[23].z 1 (the terrain's compiled-in value, which makes their tail the
//                terrain's; per material UNKNOWN); the interior weight 0 (the outdoor record: INFERRED for outdoor doodads and
//                exterior groups); the two-record path and the fog-volume composite off.
//   the record   (Pack, the Retail pattern; every row's value INFERRED (public), D1, unless stated; WorldLightModel.Fog lists
//                each rule with its grade and state). F the game far clip, v the eased light state (WorldLightSample's
//                layout with the per-key derived channels below), colours byte / 255 (the CPU's upload INFERRED):
//                  R0   (min(F, 3000) x FogScaler, F, FogDensity x 0.0005, 0)   FogScaler and FogDensity as read; the start and
//                                                                              the density are class C until in-game test T7
//                  R1   0                                                      the power path: the power36 arm only
//                  R2   (0, 1, 0, camera Z - FogHeight)                        the plane in Unity world axes (PlaneOffset)
//                  R3   (SkyFogColor, FogHeightScaler)
//                  R4   (kh x 0.0005, EndFogColor)                             derived: kh and the colour's fall-back
//                  R5   (a, SunFogColor)                                       the sky's tint inputs (SkyDome.SunTint)
//                  R6   (FogHeightColor, the colour ramp's length)             derived: the colour's fall-back, the length
//                  R7   (SunAngleBlend x SunFogStrength x the day curve, 0, 0, 0)   the sky's (SkyDome.SunTint)
//                  R8   (the sun direction, FogZScalar)                        the sky's sun path (WorldLightModel)
//                  R9   the height cubic                                       derived: reversed, all zero -> u^3
//                  R10  MainFogCoefficients reversed; R11 HeightDensityFogCoeff reversed (the order of R9: INFERRED)
//                  R12  (MainFogEndDist, MainFogStartDist, z, 1)               derived: the polynomial switch z
//                  R13  (EndFogHeightColor, FogStartOffset)                    derived: the colour's fall-back
//   the derived  (DeriveKey) every fall-back only the fog record uses is resolved on each key from that key's own values
//   channels     (the public order "fix each key, then blend", INFERRED (public)), then blended over time and point lights
//                and eased like every channel, so each row is a weighted mean of per-key values and continuous across a
//                blend: 0 z (0 where a coefficient is set and MainFogEndDist > MainFogStartDist, else 1), 1 kh in FogDensity
//                units (FogHeightDensity, or FogDensity where FogHeightScaler is 0), 2-5 the height cubic in the program's
//                order (the stored coefficients reversed; all four exactly 0 -> (1, 0, 0, 0), u^3), 6 and 7 the ramp
//                (EndFogColorDistance where it is > 0, else the far clip: R6.w = ch6 + ch7 F), 8-16 EndFogColor,
//                FogHeightColor and EndFogHeightColor with their zero fall-backs (SkyFogColor, SkyFogColor, the key's
//                resolved EndFogColor) as bytes, which the day blends with the host's rounding as it blends the stored
//                colours. The sun tint's inputs stay in the sky's order (blended, then a zero SunFogColor gives no tint), so
//                sky and fog cannot disagree. A frame whose blended polynomial end is not above its start draws without the
//                path (z 1) and says so (ArtisticRefusedBlend).
//   not applied  (data as read; the public arm's rules) the density floor 0.9, the FogScaler clamp [-1, 1] and its floors,
//                the FogEnd floor 10, the FogHeight clamps, the far-clip clamp [1000, 50000], density 1 under Map Flags[2]
//                0x2, the derived density of keys without FogDensity, the polynomial path's 0.001 separation, the
//                coefficients' 1.2e-7 epsilon, LightParams flag 0x4's tint threshold 1.1 (as the sky). No map-ID list and no
//                per-map minima exist here or anywhere.
//   the modes    (ModeFor; by client, D17 and D18: the plan's 6.9 departure (c)) wow and wowt take the Retail pattern; every
//                other product (Classic Era, MoP Classic, Classic Beta, an unlisted one) is off, unless -wmvFogClassic names
//                an arm for the side-by-sides, never compared with the game before in-game test T8: "public" = the record
//                above plus the seven rules of PublicRules, each its own row; "power36" (UNKNOWN) = the power path with end
//                FogEnd / 36, start FogScaler x end, rate the derived density with that end, SkyFogColor for all four
//                colours and no tint (a key whose end is not above its start draws no fog: PowerRefused). A light object
//                whose layout lacks a column the record is made of, or that did not send it (AbsentColumns), draws no fog
//                in any mode: the column would read 0 in every key (a black fog, a start of 0).
//   blend kinds  (KindOf) the batch's fog target: 0 the fog colour (the terrain: PROVEN_BYTES; opaque, alpha-keyed, alpha and
//                every world-model batch: INFERRED), 1 black, rgb x vis, for additive (INFERRED), 2 white for modulate and 3
//                grey 0.5 for modulate 2x (INFERRED (public)), 4 rgba x vis (the client's blend word 4) for One /
//                OneMinusSrcAlpha only (INFERRED). Any other blend pair takes kind 0 and is counted as an unknown blend.
// Sat here has Direct3D's semantics (NaN gives 0) and so do Min and Max (a NaN operand gives the other one), unlike the light
// twins' Sat (TerrainLight.cs, ModelLight.cs, SkyDome.cs), which lets NaN through: the fog twin must take its guards as the GPU
// takes them. Pack, Visibility, Colour and Apply allocate nothing.
// Thread safety: no mutable static state.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;

namespace Wmv.Wow.World
{
    /// <summary>What the fog draws (see the file header).</summary>
    public enum WorldFogMode
    {
        /// <summary>No fog: the slots are 0 and the picture is step 6's.</summary>
        Off = 0,
        /// <summary>The record of the file header (wow, wowt).</summary>
        RetailPattern = 1,
        /// <summary>-wmvFogClassic=public on a product the fog is off for (D17, not compared with the game).</summary>
        ClassicPublic = 2,
        /// <summary>-wmvFogClassic=power36 on a product the fog is off for (D17, UNKNOWN, not compared with the game).</summary>
        ClassicPower36 = 3,
    }

    /// <summary>-wmvFogClassic: the D17 candidate rules for the products the fog is off for.</summary>
    public enum WorldFogClassicArm { Off = 0, Public = 1, Power36 = 2 }

    /// <summary>One frame's fog record (WorldFog.Pack): the program's 14 rows of layer 0, x y z w each, and what the log says
    /// of it. Preallocated by its owner; Pack overwrites every field.</summary>
    public sealed class WorldFogRecord
    {
        public const int Rows = 14;

        /// <summary>Row k, component c (x 0 .. w 3) at 4 k + c.</summary>
        public readonly double[] R = new double[4 * Rows];
        public WorldFogMode Mode;
        /// <summary>The height plane's game height (yards): R2.w = camera Z - FogHeight (WorldFog.PlaneOffset).</summary>
        public double FogHeight;
        /// <summary>For the log: the start (R0.x; the power path's start), the end (R0.y; its end), k per yard (R0.z; its
        /// rate) and the fog colour (R3.xyz, authored 0..1).</summary>
        public double Start, End, K;
        public readonly double[] Colour = new double[3];
        /// <summary>The polynomial path mixed in (R12.z below 1); a blended state whose end is not above its start was drawn
        /// without it (z set to 1 this frame).</summary>
        public bool Artistic, ArtisticRefusedBlend;
        /// <summary>The power path (the power36 arm); its key's end is not above its start: no fog drawn.</summary>
        public bool Power, PowerRefused;

        public double this[int row, int c] { get { return R[4 * row + c]; } }

        public void Clear()
        {
            Array.Clear(R, 0, R.Length);
            Array.Clear(Colour, 0, Colour.Length);
            Mode = WorldFogMode.Off;
            FogHeight = Start = End = K = 0.0;
            Artistic = ArtisticRefusedBlend = Power = PowerRefused = false;
        }

        public void CopyTo(WorldFogRecord o)
        {
            Array.Copy(R, o.R, R.Length);
            Array.Copy(Colour, o.Colour, Colour.Length);
            o.Mode = Mode;
            o.FogHeight = FogHeight;
            o.Start = Start;
            o.End = End;
            o.K = K;
            o.Artistic = Artistic;
            o.ArtisticRefusedBlend = ArtisticRefusedBlend;
            o.Power = Power;
            o.PowerRefused = PowerRefused;
        }

        /// <summary>The state line's clause, the one formatter the player says (WmvWorldLight.FogText) and the runner tests: "fog:
        /// start S yd, k K /yd, end E yd, colour (r, g, b)" with the colour in whole bytes (the rig's parser reads them so), the
        /// power path's "fog: power path, start S yd, end E yd, rate R, colour (...)", an arm's mode in brackets at the end, or
        /// "fog: off" (the player says why instead).</summary>
        public string Summary()
        {
            if (Mode == WorldFogMode.Off)
                return "fog: off";
            string colour = string.Format(CultureInfo.InvariantCulture, "({0}, {1}, {2})", Math.Round(Colour[0] * 255.0), Math.Round(Colour[1] * 255.0),
                                          Math.Round(Colour[2] * 255.0));
            string arm = Mode != WorldFogMode.RetailPattern ? " [" + WorldFog.ModeName(Mode) + "]" : "";
            if (Power)
                return PowerRefused
                    ? "fog: power path refused (end not above start: no fog drawn), colour " + colour + arm
                    : string.Format(CultureInfo.InvariantCulture, "fog: power path, start {0:0.#} yd, end {1:0.#} yd, rate {2:0.###}, colour {3}{4}",
                                    Start, End, K, colour, arm);
            return string.Format(CultureInfo.InvariantCulture, "fog: start {0:0.#} yd, k {1:0.######} /yd, end {2:0.#} yd, colour {3}{4}{5}{6}",
                                 Start, K, End, colour, Artistic ? ", polynomial path mixed in" : "",
                                 ArtisticRefusedBlend ? ", polynomial path refused this frame (end not above start)" : "", arm);
        }
    }

    /// <summary>The fog's census of one light object (WorldFog.Census; the per-set log line), or of any keys added.</summary>
    public sealed class WorldFogCensus
    {
        public int Days, Keys;
        /// <summary>E: FogDensity > 0; L: FogDensity 0 (or less) and FogEnd > 0; Z: neither (k 0 as read: the end fade only).</summary>
        public int E, L, Z;
        /// <summary>Keys with a polynomial coefficient set; of them refused (MainFogEndDist not above MainFogStartDist).</summary>
        public int Polynomial, PolynomialRefused;
        /// <summary>The height coefficients all 0 with FogHeightScaler != 0 (the u^3 fall-back bites); of them FogHeightDensity 0.</summary>
        public int HeightFallBack, HeightFallBackNoDensity;
        /// <summary>kh := FogDensity acts: FogHeightScaler 0 with coefficients set summing below 0.999 (hf > 0 at every height).</summary>
        public int KhFromK;
        /// <summary>EndFogColorDistance 0 or less with EndFogColor set (the ramp-length fall-back acts).</summary>
        public int RampFallBack;
        public int SunFogSet;
        /// <summary>LightParams with flag 0x4 (the public tint threshold 1.1: not applied, as the sky).</summary>
        public int Flag4Params;
        /// <summary>E keys with FogDensity below 0.9, keys with FogScaler outside [0, 1] (both as read).</summary>
        public int DensityBelowFloor, ScalerOutside;
        public double Ms;

        public void Clear()
        {
            Days = Keys = E = L = Z = Polynomial = PolynomialRefused = HeightFallBack = HeightFallBackNoDensity = KhFromK = RampFallBack = 0;
            SunFogSet = Flag4Params = DensityBelowFloor = ScalerOutside = 0;
            Ms = 0.0;
        }

        /// <summary>One key: its colours (0xRRGGBB, WorldLightColumns.Colours' order) at colours[colourAt..] and its floats
        /// (WorldLightColumns.Floats' order) at floats[floatAt..].</summary>
        public void AddKey(int[] colours, int colourAt, double[] floats, int floatAt)
        {
            Keys++;
            double density = floats[floatAt + WorldLightColumns.FogDensity], fogEnd = floats[floatAt + WorldLightColumns.FogEnd];
            if (density > 0.0)
                E++;
            else if (fogEnd > 0.0)
                L++;
            else
                Z++;
            if (WorldFog.PolynomialSet(floats, floatAt))
            {
                Polynomial++;
                if (!(floats[floatAt + WorldLightColumns.MainFogEndDist] > floats[floatAt + WorldLightColumns.MainFogStartDist]))
                    PolynomialRefused++;
            }
            double hs = floats[floatAt + WorldLightColumns.FogHeightScaler];
            int h = floatAt + WorldLightColumns.FogHeightCoefficients;
            bool coefficients = floats[h] != 0.0 || floats[h + 1] != 0.0 || floats[h + 2] != 0.0 || floats[h + 3] != 0.0;
            if (!coefficients && hs != 0.0)
            {
                HeightFallBack++;
                if (floats[floatAt + WorldLightColumns.FogHeightDensity] == 0.0)
                    HeightFallBackNoDensity++;
            }
            if (hs == 0.0 && coefficients && floats[h] + floats[h + 1] + floats[h + 2] + floats[h + 3] < 0.999)
                KhFromK++;
            if (floats[floatAt + WorldLightColumns.EndFogColorDistance] <= 0.0 && (colours[colourAt + WorldLightColumns.EndFogColor] & 0xFFFFFF) != 0)
                RampFallBack++;
            if ((colours[colourAt + WorldLightColumns.SunFogColor] & 0xFFFFFF) != 0)
                SunFogSet++;
            if (density > 0.0 && density < WorldFog.DensityFloor)
                DensityBelowFloor++;
            double scaler = floats[floatAt + WorldLightColumns.FogScaler];
            if (scaler < 0.0 || scaler > 1.0)
                ScalerOutside++;
        }

        /// <summary>Every key of a day.</summary>
        public void AddDay(WorldLightDay d)
        {
            if (d == null)
                return;
            Days++;
            for (int k = 0; k < d.Keys; k++)
                AddKey(d.Colours, k * WorldLightColumns.ColourCount, d.Floats, k * WorldLightColumns.FloatCount);
        }

        /// <summary>The per-set log line (once per new light object).</summary>
        public string Line(int map, string product, WorldFogMode mode)
        {
            return string.Format(CultureInfo.InvariantCulture,
                                 "fog for map {0} (product {1}): {2}; keys: E {3}, L {4}, Z {5} (k 0: end fade only); polynomial path {6} ({7} refused: " +
                                 "end <= start); height-coefficient fall-back {8} (of them FogHeightDensity 0: {9}); kh := k {10}; EndFogColorDistance <= 0 " +
                                 "with EndFog set {11}; SunFog set {12}; flag 0x4 params {13} (not applied); FogDensity < 0.9 {14} and FogScaler outside " +
                                 "[0, 1] {15} (as read); {16} day(s), {17} key(s); census {18:0.###} ms",
                                 map, string.IsNullOrEmpty(product) ? "-" : product, WorldFog.ModeName(mode), E, L, Z, Polynomial, PolynomialRefused,
                                 HeightFallBack, HeightFallBackNoDensity, KhFromK, RampFallBack, SunFogSet, Flag4Params, DensityBelowFloor, ScalerOutside,
                                 Days, Keys, Ms);
        }
    }

    public static class WorldFog
    {
        // ---- the program's literals (PROVEN_BYTES, terrain 2977247 p_1; the same in the M2 and world-model programs)

        /// <summary>The end fade's slope: sat((1 - d / end) / 0.7) (p_1:190, l(1.428571); dx_6_0 0x3FF6DB6DC0000000).</summary>
        public const double EndFade = 1.0 / 0.7;
        /// <summary>The polynomial path's lead before the end ratio (p_1:211).</summary>
        public const double ArtLead = 0.3;
        /// <summary>The polynomial path's own fade slope (p_1:225, l(3.333333); dx_6_0 0x400AAAAAA0000000).</summary>
        public const double ArtFade = 1.0 / 0.3;
        public const string ProgramGrade = WorldLightModel.ProvenBytes;

        // ---- the mapping's constants (INFERRED (public) unless stated; WorldLightModel.Fog)

        /// <summary>start = min(far clip, StartCap) x FogScaler. Class C until in-game test T7.</summary>
        public const double StartCap = 3000.0;
        public const string StartCapGrade = WorldLightModel.InferredPublic;
        /// <summary>k = FogDensity x DensityScale per yard. Class C until in-game test T7.</summary>
        public const double DensityScale = 0.0005;
        public const string DensityScaleGrade = WorldLightModel.InferredPublic;

        // ---- the public arm's rules (PublicRules; "arm only": never applied on the Retail pattern)

        public const double DensityFloor = 0.9;
        public const double ScalerClamp = 1.0;
        public const double ScalerFloorDensity = -0.2, ScalerFloorNone = 0.0;
        public const double FogEndFloor = 10.0;
        public const double FogHeightOffAbove = 10000.0, FogHeightFloor = -10000.0;
        public const double FarClipMin = 1000.0, FarClipMax = 50000.0;
        /// <summary>The density under Map Flags[2] & MapFlagDensity.</summary>
        public const double FlagDensity = 1.0;
        public const long MapFlagDensity = 0x2;
        /// <summary>The derived density of a key without FogDensity: base + span (1 - diff / (min(F, cap) - sub)), diff = FogEnd -
        /// FogEnd x FogScaler; the base itself where diff is 0 or less or above min(F, cap) - sub.</summary>
        public const double DerivedDensityBase = 1.5, DerivedDensitySpan = 5.5, DerivedClipCap = 700.0, DerivedClipSub = 200.0;
        /// <summary>LightParams flag 0x4's tint threshold (recorded, not applied: the sky does not apply it).</summary>
        public const long ParamsFlagTint = 0x4;
        public const double ParamsFlagTintAngle = 1.1;

        /// <summary>The public arm's rules, as switch bits (Pack's publicRules; the tests switch each off in turn).</summary>
        public const int PublicFlagDensity = 1, PublicDerivedDensity = 2, PublicDensityFloor = 4, PublicScalerClamp = 8, PublicFogEndFloor = 16,
                         PublicHeightClamp = 32, PublicFarClipClamp = 64, PublicAll = 127;
        public static readonly string[] PublicRuleNames =
        {
            "density 1 under Map Flags[2] 0x2", "the derived density of keys without FogDensity", "the density floor 0.9",
            "the FogScaler clamp [-1, 1] and its floors", "the FogEnd floor 10", "the FogHeight clamps", "the far-clip clamp [1000, 50000]",
        };

        /// <summary>The power36 arm (UNKNOWN): end = FogEnd / PowerDivisor.</summary>
        public const double PowerDivisor = 36.0;
        public const string PowerGrade = WorldLightModel.Unknown;
        /// <summary>The power path's end for a refused key: visibility exactly 1 at every distance the view reaches.</summary>
        public const double RefusedPowerEnd = 1e30;

        // ---- the blend classes (the binding line's, WmvWorldLitMaterials aliases them) and the fog targets

        public const int BlendOpaque = 0, BlendAlphaKey = 1, BlendAlpha = 2, BlendAdditive = 3, BlendModulate = 4, BlendModulate2x = 5, BlendOther = 6,
                         BlendKinds = 7;
        /// <summary>UnityEngine.Rendering.BlendMode's values the kind rule reads (One, OneMinusSrcAlpha).</summary>
        public const int BlendModeOne = 1, BlendModeOneMinusSrcAlpha = 10;
        /// <summary>The fog target kinds (_WmvFogBatch.y): the fog colour, black, white, grey 0.5, rgba x vis.</summary>
        public const int KindColour = 0, KindBlack = 1, KindWhite = 2, KindGrey = 3, KindFade = 4, Kinds = 5;
        public static readonly string[] KindNames = { "colour", "black", "white", "grey", "fade" };

        // ---- the derived channels (WorldLightSample.FogBase + these)

        public const int DerivedPolySwitch = 0, DerivedHeightDensity = 1, DerivedHeightCubic = 2, DerivedRampLength = 6, DerivedRampFarClip = 7,
                         DerivedEndFog = 8, DerivedHeightColour = 11, DerivedEndHeightColour = 14, DerivedCount = 17;
        /// <summary>The first colour channel: from here on the derived channels are bytes, blended with the host's rounding.</summary>
        public const int DerivedColours = DerivedEndFog;

        // ---- Direct3D's saturate, min and max

        /// <summary>saturate: NaN gives 0.</summary>
        public static double Sat(double x) { return !(x > 0.0) ? 0.0 : x > 1.0 ? 1.0 : x; }

        static double Min(double a, double b) { return a < b ? a : double.IsNaN(b) ? a : b; }

        static double Max(double a, double b) { return a > b ? a : double.IsNaN(b) ? a : b; }

        // ---- modes and kinds

        /// <summary>The fog's mode for a light object's product (see the file header): Off with the fog switched off or no
        /// product; wow and wowt the Retail pattern; any other product Off, or the arm -wmvFogClassic names.</summary>
        public static WorldFogMode ModeFor(string product, bool fogOn, WorldFogClassicArm arm)
        {
            if (!fogOn || string.IsNullOrEmpty(product))
                return WorldFogMode.Off;
            if (product == "wow" || product == "wowt")
                return WorldFogMode.RetailPattern;
            return arm == WorldFogClassicArm.Public ? WorldFogMode.ClassicPublic
                 : arm == WorldFogClassicArm.Power36 ? WorldFogMode.ClassicPower36 : WorldFogMode.Off;
        }

        /// <summary>The columns the record is made of (Pack, DeriveKey and the sky's tint, SkyDome.SunTint): the five fog colours and
        /// every fog float; FogEnd only for the arms (the Retail pattern does not read it).</summary>
        static readonly int[] RecordColours =
        {
            WorldLightColumns.SkyFogColor, WorldLightColumns.EndFogColor, WorldLightColumns.SunFogColor, WorldLightColumns.FogHeightColor,
            WorldLightColumns.EndFogHeightColor,
        };
        static readonly int[] RecordFloats =
        {
            WorldLightColumns.FogEnd, WorldLightColumns.FogScaler, WorldLightColumns.FogDensity, WorldLightColumns.FogHeight, WorldLightColumns.FogHeightScaler,
            WorldLightColumns.FogHeightDensity, WorldLightColumns.FogZScalar, WorldLightColumns.MainFogStartDist, WorldLightColumns.MainFogEndDist,
            WorldLightColumns.SunFogAngle, WorldLightColumns.EndFogColorDistance, WorldLightColumns.FogStartOffset, WorldLightColumns.SunFogStrength,
            WorldLightColumns.FogHeightCoefficients, WorldLightColumns.FogHeightCoefficients + 1, WorldLightColumns.FogHeightCoefficients + 2,
            WorldLightColumns.FogHeightCoefficients + 3, WorldLightColumns.MainFogCoefficients, WorldLightColumns.MainFogCoefficients + 1,
            WorldLightColumns.MainFogCoefficients + 2, WorldLightColumns.MainFogCoefficients + 3, WorldLightColumns.HeightDensityFogCoeff,
            WorldLightColumns.HeightDensityFogCoeff + 1, WorldLightColumns.HeightDensityFogCoeff + 2, WorldLightColumns.HeightDensityFogCoeff + 3,
        };

        /// <summary>The columns `mode`'s record is made of that the light object's layout lacks or that it did not send
        /// (WorldLightSet.ColourPresent / FloatPresent), as "SkyFogColor, FogScaler" in the player's column order; null when every
        /// one is there, for the mode Off and for no object. Such a column reads 0 in every key, so WmvWorldLight draws no fog
        /// for the object instead of a wrong one. The host sends every column (absentColumns is empty on the audited clients).</summary>
        public static string AbsentColumns(WorldLightSet set, WorldFogMode mode)
        {
            if (set == null || mode == WorldFogMode.Off)
                return null;
            var absent = new List<string>();
            foreach (int c in RecordColours)
                if (!set.ColourPresent(c))
                    absent.Add(WorldLightColumns.Colours[c]);
            foreach (int f in RecordFloats)
                if (!set.FloatPresent(f) && (f != WorldLightColumns.FogEnd || mode != WorldFogMode.RetailPattern))
                    absent.Add(WorldLightColumns.Floats[f]);
            return absent.Count > 0 ? string.Join(", ", absent.ToArray()) : null;
        }

        public static string ModeName(WorldFogMode mode)
        {
            switch (mode)
            {
                case WorldFogMode.RetailPattern: return "Retail pattern";
                case WorldFogMode.ClassicPublic: return "ARM -wmvFogClassic=public (D17, not compared with the game)";
                case WorldFogMode.ClassicPower36: return "ARM -wmvFogClassic=power36 (D17, not compared with the game)";
                default: return "off";
            }
        }

        public static string ArmName(WorldFogClassicArm arm)
        {
            return arm == WorldFogClassicArm.Public ? "public" : arm == WorldFogClassicArm.Power36 ? "power36" : "off";
        }

        /// <summary>A batch's fog target (see the file header) from its blend class and blend pair (UnityEngine.Rendering.
        /// BlendMode values): kind 4 only for the class "other" with exactly One / OneMinusSrcAlpha; any other "other" pair, or
        /// a class out of range, gives kind 0 with `unknownBlend` set (counted, never faded silently).</summary>
        public static int KindOf(int blendClass, int srcBlend, int dstBlend, out bool unknownBlend)
        {
            unknownBlend = false;
            switch (blendClass)
            {
                case BlendOpaque:
                case BlendAlphaKey:
                case BlendAlpha:
                    return KindColour;
                case BlendAdditive:
                    return KindBlack;
                case BlendModulate:
                    return KindWhite;
                case BlendModulate2x:
                    return KindGrey;
                case BlendOther:
                    if (srcBlend == BlendModeOne && dstBlend == BlendModeOneMinusSrcAlpha)
                        return KindFade;
                    unknownBlend = true;
                    return KindColour;
                default:
                    unknownBlend = true;
                    return KindColour;
            }
        }

        /// <summary>The binding line's fog clause, from the bound batches' counts: `unfogged` (NoFog /
        /// F_UNFOGGED), `kinds` per fog target (KindColour .. KindFade), the unknown blends and the world-model batches whose MOMT
        /// blend is 2 or above. Starts with "; ".</summary>
        public static string BindingClause(int unfogged, int[] kinds, int unknownBlend, int momtBlend2)
        {
            return string.Format(CultureInfo.InvariantCulture,
                                 "; fog: {0} unfogged (NoFog / F_UNFOGGED), targets: colour {1}, black {2}, white {3}, grey {4}, fade {5}, unknown blend {6}; " +
                                 "world-model MOMT blend 2+: {7}",
                                 unfogged, kinds[KindColour], kinds[KindBlack], kinds[KindWhite], kinds[KindGrey], kinds[KindFade], unknownBlend, momtBlend2);
        }

        // ---- the derived channels

        /// <summary>Is any of the eight polynomial coefficients (MainFogCoefficients, HeightDensityFogCoeff) of a key non-zero?</summary>
        public static bool PolynomialSet(double[] floats, int floatAt)
        {
            int m = floatAt + WorldLightColumns.MainFogCoefficients, h = floatAt + WorldLightColumns.HeightDensityFogCoeff;
            for (int k = 0; k < 4; k++)
                if (floats[m + k] != 0.0 || floats[h + k] != 0.0)
                    return true;
            return false;
        }

        /// <summary>One key's derived fog channels (see the file header) from its own stored colours (0xRRGGBB at
        /// colours[colourAt..], WorldLightColumns.Colours' order) and floats (floats[floatAt..], WorldLightColumns.Floats' order),
        /// into derived[derivedAt .. derivedAt + DerivedCount): channels 0-7 as values, 8-16 as bytes 0..255.</summary>
        public static void DeriveKey(int[] colours, int colourAt, double[] floats, int floatAt, double[] derived, int derivedAt)
        {
            derived[derivedAt + DerivedPolySwitch] =
                PolynomialSet(floats, floatAt) && floats[floatAt + WorldLightColumns.MainFogEndDist] > floats[floatAt + WorldLightColumns.MainFogStartDist]
                    ? 0.0 : 1.0;
            derived[derivedAt + DerivedHeightDensity] = floats[floatAt + WorldLightColumns.FogHeightScaler] != 0.0
                ? floats[floatAt + WorldLightColumns.FogHeightDensity] : floats[floatAt + WorldLightColumns.FogDensity];
            int h = floatAt + WorldLightColumns.FogHeightCoefficients;
            int c = derivedAt + DerivedHeightCubic;
            if (floats[h] == 0.0 && floats[h + 1] == 0.0 && floats[h + 2] == 0.0 && floats[h + 3] == 0.0)
            {
                derived[c] = 1.0;                       // u^3 (the stored order's (0, 0, 0, 1))
                derived[c + 1] = derived[c + 2] = derived[c + 3] = 0.0;
            }
            else
                for (int k = 0; k < 4; k++)
                    derived[c + k] = floats[h + 3 - k];  // stored constant first, the program's x^3 first
            double ramp = floats[floatAt + WorldLightColumns.EndFogColorDistance];
            derived[derivedAt + DerivedRampLength] = ramp > 0.0 ? ramp : 0.0;
            derived[derivedAt + DerivedRampFarClip] = ramp > 0.0 ? 0.0 : 1.0;
            int sky = colours[colourAt + WorldLightColumns.SkyFogColor] & 0xFFFFFF;
            int end = colours[colourAt + WorldLightColumns.EndFogColor] & 0xFFFFFF;
            int height = colours[colourAt + WorldLightColumns.FogHeightColor] & 0xFFFFFF;
            int endHeight = colours[colourAt + WorldLightColumns.EndFogHeightColor] & 0xFFFFFF;
            if (end == 0)
                end = sky;
            if (height == 0)
                height = sky;
            if (endHeight == 0)
                endHeight = end;
            Bytes(end, derived, derivedAt + DerivedEndFog);
            Bytes(height, derived, derivedAt + DerivedHeightColour);
            Bytes(endHeight, derived, derivedAt + DerivedEndHeightColour);
        }

        static void Bytes(int rgb, double[] d, int at)
        {
            d[at] = (rgb >> 16) & 0xFF;
            d[at + 1] = (rgb >> 8) & 0xFF;
            d[at + 2] = rgb & 0xFF;
        }

        /// <summary>A derived channel as the light sample holds it: the colour channels byte / 255, the others as they are.</summary>
        public static double SampleValue(int channel, double value) { return channel >= DerivedColours ? value / 255.0 : value; }

        /// <summary>The public derived density (see DerivedDensityBase) of a key with `fogEnd`, `scaler`, at far clip `farClip`.</summary>
        public static double DerivedDensity(double fogEnd, double scaler, double farClip)
        {
            double cap = Math.Min(farClip, DerivedClipCap) - DerivedClipSub;
            double diff = fogEnd - fogEnd * scaler;
            if (diff > cap || diff <= 0.0)
                return DerivedDensityBase;
            return (1.0 - diff / cap) * DerivedDensitySpan + DerivedDensityBase;
        }

        // ---- the record

        /// <summary>The height plane's offset R2.w for a camera at game height `cameraGameZ` (yards): cameraGameZ - FogHeight, so
        /// that the plane term (p.y + R2.w) x R3.w with p = fragment - camera in Unity world axes reads (fragment Z - FogHeight) x
        /// rate without the floating origin.</summary>
        public static double PlaneOffset(double cameraGameZ, WorldFogRecord r) { return cameraGameZ - r.FogHeight; }

        static void Put(double[] r, int row, double x, double y, double z, double w)
        {
            int at = 4 * row;
            r[at] = x;
            r[at + 1] = y;
            r[at + 2] = z;
            r[at + 3] = w;
        }

        static double Float(double[] v, int column) { return v[WorldLightSample.FloatAt(column)]; }

        /// <summary>The frame's record (see the file header) from the eased state `v` (WorldLightSample's layout), the game far
        /// clip, the camera's game height, the light's half-minute, the sun direction in Unity world axes (the vector the sky
        /// gets), the map's three flag words and the mode. Off clears the record. Nothing is allocated.</summary>
        public static void Pack(double[] v, double farClip, double cameraGameZ, int halfMinute, double[] sunUnity, long[] mapFlags, WorldFogMode mode,
                                WorldFogRecord r)
        {
            Pack(v, farClip, cameraGameZ, halfMinute, sunUnity, mapFlags, mode, PublicAll, r);
        }

        /// <summary>Pack with the public arm's rules limited to `publicRules` (Public* bits; the tests switch each off in turn).
        /// Every other mode ignores it.</summary>
        public static void Pack(double[] v, double farClip, double cameraGameZ, int halfMinute, double[] sunUnity, long[] mapFlags, WorldFogMode mode,
                                int publicRules, WorldFogRecord r)
        {
            r.Clear();
            r.Mode = mode;
            if (mode == WorldFogMode.Off)
                return;
            double[] R = r.R;
            int fb = WorldLightSample.FogBase;
            double far = farClip;
            double fogEnd = Float(v, WorldLightColumns.FogEnd), scaler = Float(v, WorldLightColumns.FogScaler);
            double density = Float(v, WorldLightColumns.FogDensity), fogHeight = Float(v, WorldLightColumns.FogHeight);
            if (mode == WorldFogMode.ClassicPublic)
            {
                bool flag = mapFlags != null && mapFlags.Length > 2 && (mapFlags[2] & MapFlagDensity) != 0;
                if ((publicRules & PublicFarClipClamp) != 0)
                    far = Math.Min(Math.Max(far, FarClipMin), FarClipMax);
                if ((publicRules & PublicFogEndFloor) != 0)
                    fogEnd = Math.Max(fogEnd, FogEndFloor);
                if ((publicRules & PublicScalerClamp) != 0)
                    scaler = Math.Min(Math.Max(scaler, -ScalerClamp), ScalerClamp);
                bool authored = density > 0.0;
                if (!authored && (publicRules & PublicDerivedDensity) != 0)
                    density = DerivedDensity(fogEnd, scaler, far);
                if ((publicRules & PublicDensityFloor) != 0)
                    density = Math.Max(density, DensityFloor);
                if (flag)
                {
                    if ((publicRules & PublicFlagDensity) != 0)
                        density = FlagDensity;
                }
                else if ((publicRules & PublicScalerClamp) != 0)
                    scaler = Math.Max(scaler, authored ? ScalerFloorDensity : ScalerFloorNone);
                if ((publicRules & PublicHeightClamp) != 0)
                    fogHeight = fogHeight > FogHeightOffAbove ? 0.0 : Math.Max(fogHeight, FogHeightFloor);
            }
            r.FogHeight = fogHeight;
            Put(R, 0, Math.Min(far, StartCap) * scaler, far, density * DensityScale, 0.0);
            Put(R, 2, 0.0, 1.0, 0.0, PlaneOffset(cameraGameZ, r));
            int sky = WorldLightSample.ColourAt(WorldLightColumns.SkyFogColor);
            Put(R, 3, v[sky], v[sky + 1], v[sky + 2], Float(v, WorldLightColumns.FogHeightScaler));
            Put(R, 4, v[fb + DerivedHeightDensity] * DensityScale, v[fb + DerivedEndFog], v[fb + DerivedEndFog + 1], v[fb + DerivedEndFog + 2]);
            double angle, dayCurve, weight;
            SkyDome.SunTint(v, halfMinute, R, 21, out angle, out dayCurve, out weight);   // R5.yzw: SunFogColor
            R[20] = angle;
            Put(R, 6, v[fb + DerivedHeightColour], v[fb + DerivedHeightColour + 1], v[fb + DerivedHeightColour + 2],
                v[fb + DerivedRampLength] + v[fb + DerivedRampFarClip] * far);
            Put(R, 7, weight, 0.0, 0.0, 0.0);
            Put(R, 8, sunUnity[0], sunUnity[1], sunUnity[2], Float(v, WorldLightColumns.FogZScalar));
            Put(R, 9, v[fb + DerivedHeightCubic], v[fb + DerivedHeightCubic + 1], v[fb + DerivedHeightCubic + 2], v[fb + DerivedHeightCubic + 3]);
            int m = WorldLightSample.FloatAt(WorldLightColumns.MainFogCoefficients), hd = WorldLightSample.FloatAt(WorldLightColumns.HeightDensityFogCoeff);
            Put(R, 10, v[m + 3], v[m + 2], v[m + 1], v[m]);
            Put(R, 11, v[hd + 3], v[hd + 2], v[hd + 1], v[hd]);
            double artEnd = Float(v, WorldLightColumns.MainFogEndDist), artStart = Float(v, WorldLightColumns.MainFogStartDist);
            double z = v[fb + DerivedPolySwitch];
            if (z < 1.0 && !(artEnd > artStart))
            {
                z = 1.0;                                // only while blending with a refused key: drawn without the path, said
                r.ArtisticRefusedBlend = true;
            }
            r.Artistic = z < 1.0;
            Put(R, 12, artEnd, artStart, z, 1.0);
            Put(R, 13, v[fb + DerivedEndHeightColour], v[fb + DerivedEndHeightColour + 1], v[fb + DerivedEndHeightColour + 2],
                Float(v, WorldLightColumns.FogStartOffset));
            r.Start = R[0];
            r.End = R[1];
            r.K = R[2];
            if (mode == WorldFogMode.ClassicPower36)
            {
                // The power path (UNKNOWN): FogEnd / 36, the start from FogScaler, the derived density as the rate; SkyFogColor for
                // all four colours and no tint.
                double end = fogEnd / PowerDivisor;
                double start = scaler * end;
                double rate = DerivedDensity(end, scaler, far);
                r.Power = true;
                r.Artistic = r.ArtisticRefusedBlend = false;
                if (end > start && rate > 0.0)
                    Put(R, 1, 1.0, end, 1.0 / (end - start), rate);
                else
                {
                    r.PowerRefused = true;
                    Put(R, 1, 1.0, RefusedPowerEnd, 1.0, 1.0);
                }
                for (int k = 0; k < 3; k++)
                    R[17 + k] = R[24 + k] = R[52 + k] = R[12 + k];
                R[20] = 1.0;
                R[28] = 0.0;
                r.Start = start;
                r.End = end;
                r.K = rate;
            }
            r.Colour[0] = R[12];
            r.Colour[1] = R[13];
            r.Colour[2] = R[14];
        }

        // ---- the CPU twin of WmvWorldFog.hlsl (operation for operation, in double)

        /// <summary>c.x x^3 + c.y x^2 + c.z x + c.w of row `row`, in the program's order (p_1:175-180).</summary>
        static double Cubic(double[] r, int row, double x)
        {
            int at = 4 * row;
            double x2 = x * x;
            return (r[at + 2] * x + (r[at] * (x * x2) + x2 * r[at + 1])) + r[at + 3];
        }

        /// <summary>The record's visibility (1: no fog) and height factor at fog position `p` (camera-relative, the record's
        /// frame) with the program's up row `up` (the player and the GPU fixtures pass (0, 1, 0); the tests also the client's
        /// own (0, 0, 1)). WmvWorldFog.hlsl's WmvFogVisibility.</summary>
        public static void Visibility(WorldFogRecord r, double[] p, double[] up, out double vis, out double hf)
        {
            double[] R = r.R;
            double raw = Math.Sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            if (R[4] != 0.0)
            {
                vis = Min(Math.Pow(Max((R[5] - raw) * R[6], 0.0), R[7]), 1.0);   // the power path (p_1:236-244)
                hf = 0.0;
                return;
            }
            double m = Max(R[35], 0.0);                                           // the vertical pull (p_1:141, 155-163)
            double t = Min(Max(up[0] * p[0] + up[1] * p[1] + up[2] * p[2], -m), m);
            double qx = p[0] - up[0] * t, qy = p[1] - up[1] * t, qz = p[2] - up[2] * t;
            double d = Math.Sqrt(qx * qx + qy * qy + qz * qz);
            double D = Max(d - R[3] - R[0], 0.0);                                 // the two start terms (p_1:164-166)
            double u = 1.0 - Sat((R[8] * p[0] + R[9] * p[1] + R[10] * p[2] + R[11]) * R[15]);   // the height plane (p_1:171-174)
            hf = 1.0 - Sat(Cubic(R, 9, u));                                        // p_1:175-181
            double eMain = Math.Exp(-R[2] * D);                                    // p_1:183-186
            double eHeight = Math.Exp(-R[16] * D);                                 // p_1:167-170
            double leg = Min(eMain + hf * (eHeight - eMain), Sat((1.0 - d / R[1]) * EndFade));   // p_1:182, 187-191
            vis = leg;
            if (R[50] < 1.0)                                                       // the polynomial path, mixed in by R12.z (p_1:192-233)
            {
                double tt = Sat((d - R[49]) / (R[48] - R[49]));
                double q = Sat(d / R[48]);
                double a10 = 1.0 - Sat(Cubic(R, 10, tt));
                double a11 = 1.0 - Sat(Cubic(R, 11, tt));
                double A = a10 + hf * (a11 - a10);
                double e = Sat(R[1] / R[48]) - ArtLead;
                double b10 = Sat(Cubic(R, 10, e));
                double b11 = Sat(Cubic(R, 11, e));
                double B = b10 + hf * (b11 - b10);
                double w = Sat((q - e) * ArtFade);
                double art = Min(A + w * (Sat(1.0 - (B + w * (1.0 - B))) - A), A);
                vis = art + R[50] * (leg - art);
            }
            // The strength 1 - s (1 - vis) is held at s = 1: the expression is vis.
        }

        /// <summary>The record's fog colour at `p` with height factor `hf` into fc[0..2] (p_1:246-265; WmvFogColour). `up` is
        /// not read (the colour has no up term); it keeps Visibility's signature.</summary>
        public static void Colour(WorldFogRecord r, double[] p, double[] up, double hf, double[] fc)
        {
            double[] R = r.R;
            double raw = Math.Sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            double c = Sat((raw - R[55]) / R[27]);                                 // raw distance (p_1:246-247)
            double c3 = c * (c * c);
            double x = raw > 0.0 ? Sat(Sat(p[0] / raw * R[32] + p[1] / raw * R[33] + p[2] / raw * R[34]) - R[20]) : 0.0;   // p_1:254, 257
            double s = x > 0.0 ? x / (1.0 - R[20]) : 0.0;                         // p_1:259-261; discarded where x is 0 (a select)
            double s3 = s * (s * s);
            for (int k = 0; k < 3; k++)
            {
                double baseC = R[12 + k] + c3 * (R[17 + k] - R[12 + k]);         // c^3 (p_1:248-249, 251)
                double hgtC = R[24 + k] + c * (R[52 + k] - R[24 + k]);           // c (p_1:250)
                double f = baseC + hf * (hgtC - baseC);                           // p_1:252-253
                fc[k] = x > 0.0 ? f + s3 * (R[28] * (R[21 + k] - f)) : f;        // the tint, not saturated (p_1:255-256, 262-265)
            }
        }

        /// <summary>The fogged colour of `litRgba` (after the light, the lobes and the alpha) at fog position `p`, for fog target
        /// `kind` (KindColour .. KindFade), into outRgba (it may be litRgba): WmvWorldFog.hlsl's WmvWorldFog. Where the
        /// visibility is not below 1 the input is returned itself (the output select).</summary>
        public static void Apply(WorldFogRecord r, double[] p, double[] up, double[] litRgba, int kind, double[] outRgba)
        {
            double l0 = litRgba[0], l1 = litRgba[1], l2 = litRgba[2], l3 = litRgba[3];
            double vis, hf;
            Visibility(r, p, up, out vis, out hf);
            double t0, t1, t2;
            if (kind < KindBlack)
            {
                Colour(r, p, up, hf, outRgba);
                t0 = outRgba[0];
                t1 = outRgba[1];
                t2 = outRgba[2];
            }
            else
                t0 = t1 = t2 = kind == KindWhite ? 1.0 : kind == KindGrey ? 0.5 : 0.0;
            bool fogged = vis < 1.0;
            outRgba[0] = fogged ? l0 * vis + t0 * (1.0 - vis) : l0;
            outRgba[1] = fogged ? l1 * vis + t1 * (1.0 - vis) : l1;
            outRgba[2] = fogged ? l2 * vis + t2 * (1.0 - vis) : l2;
            outRgba[3] = kind >= KindFade && fogged ? l3 * vis : l3;
        }

        // ---- the census

        /// <summary>The fog census of a light object (once per new set): every distinct slot-0 day its lights name (the zones', the
        /// point lights' and the default light's), its keys counted by WorldFogCensus.AddKey, and the slot-0 LightParams with flag
        /// 0x4. Null gives an empty census.</summary>
        public static WorldFogCensus Census(WorldLightSet set)
        {
            var c = new WorldFogCensus();
            if (set == null)
                return c;
            Stopwatch watch = Stopwatch.StartNew();
            var lights = new HashSet<int>();
            foreach (WorldLightZone z in set.Zones)
                lights.Add(z.Light);
            foreach (WorldLightPoint p in set.Points)
                lights.Add(p.Light);
            if (set.DefaultLight > 0)
                lights.Add(set.DefaultLight);
            var days = new HashSet<int>();
            var parameters = new HashSet<int>();
            foreach (int light in lights)
            {
                bool fellBack;
                WorldLightParams p = set.ParamsOf(light, 0, out fellBack);
                if (p == null)
                    continue;
                if (parameters.Add(p.Id) && (p.Flags & ParamsFlagTint) != 0)
                    c.Flag4Params++;
                if (days.Add(p.Day))
                    c.AddDay(set.DayAt(p.Day));
            }
            c.Ms = watch.Elapsed.TotalMilliseconds;
            return c;
        }
    }
}
