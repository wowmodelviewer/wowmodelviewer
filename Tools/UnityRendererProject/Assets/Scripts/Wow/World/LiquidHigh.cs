// LiquidHigh.cs
//
// THE HIGH WATER LOOK (Stage 09 tuning): the pure parts of the water's HIGH path (Resources/WmvLiquid.shader). Pure C#, no
// UnityEngine: the player applies them (WmvLiquidMaterials, WmvWaterGrabPass), Tests/LiquidHighTests.cs checks them, and the
// lifecycle self-test takes its expected pixels from them.
//   LiquidHighMath   the formulas the shader evaluates per fragment, on numbers: the transmission T(Ls), the water colour
//                    W (the zone light's pair x waterGain; without a zone light the type's colour x noZoneGain, saturated),
//                    Schlick's Fresnel, the refraction offset's ramp and the rule that accepts its shifted sample (behind
//                    the water point AND not above the water), a reflection texel's coverage (the grab's background texels
//                    carry none: the sky fills in), the mix of the refracted and reflected colours (the two reflection
//                    shares scaled to a sum of at most 1, the output saturated), the ripple map's offset for the scene's
//                    floating origin (the ripples on the game's axes whatever the origin), which side of the sheet the
//                    camera sees (UpperSide: the triangle's own facing, which decides the HIGH path) and the ripple map's
//                    mean tilt taken out of a normal (RemoveTilt).
//   LiquidHighTune   the TUNABLE values -- their names, start values and ranges -- and the -wmvWaterTune string that
//                    overrides them at run time ("key:value,key:value"), so a tuning render needs no rebuild; and which
//                    tokens of a command line are its pairs (TuneTokens: WMV_DEBUG splits on commas).
//   LiquidBump       the check that a ripple map is a unit normal map before it is drawn (else flat normals), its slope
//                    spread with the strength factor that brings it to the tuned map's (StrengthFactor), and its mean
//                    slope: the map's constant tilt, which the shader takes out of every sample (_HiBumpMean).
// The start values are the tuned defaults (WmvLiquidMaterials.Tune): each was MEASURED at ONE spot (Southfury River, type 5
// Slow Water, the PTR at Liquid Detail High, noon; tuned against the four High shots there; noZoneGain at the same spot in
// Classic Beta, type 1240 without a zone light) or is a documented choice; all of them are applied to every other water
// and ocean type as INFERRED. The mechanism is the viewer's approximation of the
// picture, never a claim about the game's shader.

using System;
using System.Collections.Generic;
using System.Globalization;

namespace Wmv.Wow.World
{
    /// <summary>The HIGH path's formulas (the shader's, in doubles). Ls is the straight path in yards from the water point to
    /// the scene point behind it.</summary>
    public static class LiquidHighMath
    {
        public static double Saturate(double v) { return v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v; }

        /// <summary>The share of the bed's contrast that reaches the eye: min(1, fastA exp(-Ls / fastL) + slowA exp(-Ls /
        /// slowL)) -- MEASURED as a fit of the bed-contrast share against Ls (RMS 0.026). A negative Ls counts as 0.</summary>
        public static double Transmission(double ls, double fastA, double fastL, double slowA, double slowL)
        {
            double l = Math.Max(0.0, ls);
            return Math.Min(1.0, fastA * Math.Exp(-l / fastL) + slowA * Math.Exp(-l / slowL));
        }

        /// <summary>One channel of the colour the bed fades to under a zone light: lerp(close, far, saturate(Ls / depthFull)) x
        /// gain (waterGain: the zone light's colours only).</summary>
        public static double Water(double close, double far, double ls, double depthFull, double gain)
        {
            return (close + (far - close) * Saturate(ls / depthFull)) * gain;
        }

        /// <summary>One channel of the colour the bed fades to WITHOUT a zone light: saturate(the type's MinimapStaticCol x
        /// gain) (noZoneGain) -- never past 1, whatever the type stores (Classic Beta's PBR water stores (182, 244, 245)).</summary>
        public static double WaterNoZone(double baseColour, double gain)
        {
            return Saturate(baseColour * gain);
        }

        /// <summary>How far above the water point (yd) a shifted refraction sample's scene point may lie and still be the bed
        /// (the water's own surface, depth-quantised).</summary>
        public const double AboveWaterTolerance = 0.02;

        /// <summary>Is the shifted refraction sample the bed? Its scene point lies behind the water point along the view ray
        /// (dot(SR - P, V) >= 0) AND not above the water (SR.y <= P.y + AboveWaterTolerance); else the unshifted sample is
        /// used. The second condition keeps the far bank, rocks and trunks that rise out of the water behind P out of the
        /// refracted bed.</summary>
        public static bool AcceptRefracted(double dotSrMinusPWithV, double srY, double pY)
        {
            return dotSrMinusPWithV >= 0.0 && srY <= pY + AboveWaterTolerance;
        }

        /// <summary>One channel of a reflection texel as the grab carries it: premultiplied by its coverage `a` (0 where the
        /// scene's depth is the far plane, the background; bilinear and blurred texels a share), the sky filling the rest --
        /// c + sky (1 - a). The camera's clear colour never reaches a reflection.</summary>
        public static double Reflected(double premultiplied, double coverage, double sky)
        {
            return premultiplied + sky * (1.0 - Saturate(coverage));
        }

        /// <summary>The broad and detail reflection shares (gain x Schlick each) scaled so their sum is at most 1 (the
        /// refracted share 1 - rb - rd never negative).</summary>
        public static void LimitReflection(ref double rb, ref double rd)
        {
            rb = Math.Max(0.0, rb);
            rd = Math.Max(0.0, rd);
            double s = rb + rd;
            if (s > 1.0)
            {
                rb /= s;
                rd /= s;
            }
        }

        /// <summary>a mod m in [0, m) (m > 0).</summary>
        public static double Mod(double a, double m)
        {
            double r = a - m * Math.Floor(a / m);
            return r >= m || r < 0.0 ? 0.0 : r;
        }

        /// <summary>The ripple map's offset for one scale (`tile` yd per repeat): the scene's world xz (Unity, relative to the
        /// floating origin) plus this is the absolute position on the game's axes in the scene's axis sense, (-Y, X), modulo
        /// the tile -- the ripples stay where they are however the scene was entered. (originX, originY): the game position
        /// that Unity's (0, 0, 0) stands for (WorldToUnity: ux = -(y - originY), uz = x - originX). Computed in double, so
        /// the float the shader adds stays small.</summary>
        public static void RippleOffset(double originX, double originY, double tile, out double offsetX, out double offsetZ)
        {
            offsetX = Mod(-originY, tile);
            offsetZ = Mod(originX, tile);
        }

        /// <summary>Schlick's Fresnel at cos i (the view ray against the surface normal): f0 + (1 - f0) (1 - cos i)^5.</summary>
        public static double Schlick(double cosI, double f0)
        {
            return f0 + (1.0 - f0) * Math.Pow(1.0 - Saturate(cosI), 5.0);
        }

        /// <summary>The refraction offset's ramp: saturate(Ls / ramp) -- no offset where the water is thin.</summary>
        public static double RefractRamp(double ls, double ramp)
        {
            return Saturate(ls / ramp);
        }

        /// <summary>One channel of the output: saturate(refracted (1 - rb - rd) + reflBlur rb + reflSharp rd), where rb and rd
        /// are the broad and detail gains times Schlick, scaled to a sum of at most 1 (LimitReflection). Saturated to the
        /// authored 0..1: nothing the water draws can bloom.</summary>
        public static double Mix(double refracted, double reflBlur, double reflSharp, double rb, double rd)
        {
            LimitReflection(ref rb, ref rd);
            return Saturate(refracted * (1.0 - rb - rd) + reflBlur * rb + reflSharp * rd);
        }

        /// <summary>One channel of the refracted colour: lerp(W, bed, T).</summary>
        public static double Refracted(double water, double bed, double t)
        {
            return water + (bed - water) * t;
        }

        /// <summary>How far the unit view ray must point into the sheet -- against its up normal; on flat water, down -- for
        /// the HIGH path (the shader's WMV_VIEW_DOWN).</summary>
        public const double ViewDown = 1e-4;
        /// <summary>The up normal's length (yd^2: the cross of two screen derivatives) at or below which the derivatives give
        /// no normal and the view ray alone decides (the shader's WMV_FACING_MIN).</summary>
        public const double FacingMin = 1e-12;

        /// <summary>Does the camera see the sheet's upper side, so the HIGH path draws the pixel? The triangle's up normal
        /// nUp = cross(dpdy, dpdx) (the water point's screen derivatives along x and y), turned so nUp.y >= 0, against the
        /// unit view ray v: dot(v, nUp) < -ViewDown |nUp|. Where |nUp| <= FacingMin the derivatives give no normal and the
        /// view ray going down decides, -v.y > ViewDown -- on flat water the two are the same test. A sloped sheet whose far
        /// part rises above the eye is still seen from above there; a sheet whose plane passes above the eye is seen from
        /// below, even where the ray runs down. No winding is relied on.</summary>
        public static bool UpperSide(double vx, double vy, double vz, double dxX, double dxY, double dxZ, double dyX, double dyY, double dyZ)
        {
            double nx = dyY * dxZ - dyZ * dxY, ny = dyZ * dxX - dyX * dxZ, nz = dyX * dxY - dyY * dxX;
            if (ny < 0.0)
            {
                nx = -nx;
                ny = -ny;
                nz = -nz;
            }
            double len = Math.Sqrt(nx * nx + ny * ny + nz * nz);
            if (len > FacingMin)
                return vx * nx + vy * ny + vz * nz < -ViewDown * len;
            return -vy > ViewDown;
        }

        /// <summary>The ripple map's mean tilt taken out of one normal (n's x and y; z kept), as the shader does before the
        /// strength scale: (nx, ny) - (meanX, meanY) nz -- the slope (nx / nz, ny / nz) less the map's mean slope. Linear,
        /// so the same on each sample or on their weighted blend.</summary>
        public static void RemoveTilt(ref double nx, ref double ny, double nz, double meanX, double meanY)
        {
            nx -= meanX * nz;
            ny -= meanY * nz;
        }

        // ---------------------------------------------------------------- Stage 10 step 8: the water's fog

        /// <summary>The water fog's arms (-wmvWaterFog; the shader's _WmvWaterFog): none, the surface terms (the default), the
        /// client's literal form on the whole composite.</summary>
        public const int WaterFogOff = 0, WaterFogSurface = 1, WaterFogComposite = 2;

        /// <summary>One channel of a reflection's two shares on the HIGH path: of the sky, skyShare = 1 - fade sat(a) on a hit,
        /// 1 on a miss; of the scene (the grab's premultiplied texel c), scene = fade c on a hit, 0 on a miss. The reflection is
        /// sky skyShare + scene -- lerp(sky, c + sky (1 - sat(a)), fade), the shader's form, rearranged.</summary>
        public static void ReflectionShares(bool hit, double fade, double premultiplied, double coverage, out double skyShare, out double scene)
        {
            skyShare = hit ? 1.0 - fade * Saturate(coverage) : 1.0;
            scene = hit ? fade * premultiplied : 0.0;
        }

        /// <summary>
        /// Stage 10 step 8: one channel of the HIGH output with the client's fog of visibility `vis` and colour `fogC` (the same
        /// domain as the rest), arm `arm`. Stage 09's output is saturate(W (1 - T)(1 - r) + bed T (1 - r) + reflB rb + reflS rd),
        /// r = rb + rd after LimitReflection, each reflection sky sX + fade cX on a hit (ReflectionShares), the sky on a miss.
        ///   WaterFogSurface  the SURFACE terms -- S = W (1 - T)(1 - r) + sky (rb sB + rd sS), of weight wS = (1 - T)(1 - r) +
        ///                    rb sB + rd sS -- go towards the fog colour, the SCENE terms -- bed T (1 - r) + rb fade cB + rd fade cS,
        ///                    the grab's copies, already fogged at their own distance -- are never fogged again:
        ///                    saturate(scene + vis S + (1 - vis) fogC wS) (INFERRED: the owner's rule, a deliberate departure from
        ///                    the client, whose water programs fog the whole composite, PROVEN_BYTES)
        ///   WaterFogComposite the client's literal form: saturate(lerp(fogC, Stage 09's output, vis)) (the refraction and the
        ///                    reflected texels fogged again)
        ///   WaterFogOff      Stage 09's output.
        /// Where vis is 1 or more (inside the fog start) Stage 09's output itself, as the shader's select.
        /// </summary>
        public static double MixFogged(double water, double bed, double t, double sky, double rb, double rd, bool hit, double fade, double blurC,
                                       double blurA, double sharpC, double sharpA, double vis, double fogC, int arm)
        {
            LimitReflection(ref rb, ref rd);
            double sB, xB, sS, xS;
            ReflectionShares(hit, fade, blurC, blurA, out sB, out xB);
            ReflectionShares(hit, fade, sharpC, sharpA, out sS, out xS);
            double reflB = hit ? sky + (Reflected(blurC, blurA, sky) - sky) * fade : sky;
            double reflS = hit ? sky + (Reflected(sharpC, sharpA, sky) - sky) * fade : sky;
            double stage09 = Saturate(Refracted(water, bed, t) * (1.0 - rb - rd) + reflB * rb + reflS * rd);
            if (arm == WaterFogOff || !(vis < 1.0))
                return stage09;
            if (arm == WaterFogComposite)
                return Saturate(fogC + (stage09 - fogC) * vis);
            double r = rb + rd;
            double wS = SurfaceWeight(t, rb, rd, sB, sS);
            double surface = water * ((1.0 - t) * (1.0 - r)) + sky * (rb * sB + rd * sS);
            double scene = bed * (t * (1.0 - r)) + rb * xB + rd * xS;
            return Saturate(scene + vis * surface + (1.0 - vis) * fogC * wS);
        }

        /// <summary>The surface terms' weight wS = (1 - T)(1 - r) + rb sB + rd sS (rb, rd already limited; sX the reflections'
        /// sky shares): the share of the HIGH output the fog colour takes at visibility 0.</summary>
        public static double SurfaceWeight(double t, double rb, double rd, double skyShareB, double skyShareS)
        {
            return (1.0 - t) * (1.0 - (rb + rd)) + rb * skyShareB + rd * skyShareS;
        }

        /// <summary>Stage 10 step 8: one channel of a PREMULTIPLIED output (the zone and fallback paths, and an opaque liquid at
        /// alpha 1) fogged: vis rgb + (1 - vis) fogC alpha -- the client's lerp towards the fog colour on the straight colour,
        /// premultiplied by the output alpha, which the client carries through its fog (PROVEN_BYTES: never x vis); the
        /// destination's share (the scene behind, fogged at its own distance) is untouched (INFERRED). Where vis is 1 or more the
        /// input itself.</summary>
        public static double FogPremultiplied(double rgb, double alpha, double vis, double fogC)
        {
            return !(vis < 1.0) ? rgb : rgb * vis + fogC * (alpha * (1.0 - vis));
        }
    }

    /// <summary>One TUNABLE value: its -wmvWaterTune key, start value and the range a value must lie in.</summary>
    public sealed class LiquidHighKey
    {
        public readonly string Key;
        public readonly double Start, Min, Max;

        public LiquidHighKey(string key, double start, double min, double max)
        {
            Key = key;
            Start = start;
            Min = min;
            Max = max;
        }
    }

    /// <summary>
    /// The HIGH path's TUNABLE values and the -wmvWaterTune override ("key:value,key:value"; WMV_DEBUG splits on commas, so
    /// the pairs may also come as separate tokens). An unknown key, a value that is not a number or one out of its range is
    /// ignored and said (Describe); a key given twice keeps the last value.
    /// </summary>
    public sealed class LiquidHighTune
    {
        // Keys (the shader property in brackets; WmvLiquidMaterials maps them).
        public const string Refract = "refract", RefractRamp = "refractRamp", TFastA = "tFastA", TFastL = "tFastL", TSlowA = "tSlowA",
                            TSlowL = "tSlowL", WaterGain = "waterGain", ReflBroad = "reflBroad", ReflDetail = "reflDetail", F0 = "f0",
                            ReflBump = "reflBump", ReflMaxDist = "reflMaxDist", ReflThick = "reflThick", ReflThickPct = "reflThickPct",
                            ReflEdge = "reflEdge", BumpTileA = "bumpTileA", BumpTileB = "bumpTileB", BumpScroll = "bumpScroll",
                            BumpWeightA = "bumpWeightA", BumpStrength = "bumpStrength", SkyR = "skyR", SkyG = "skyG", SkyB = "skyB",
                            GrabBlur = "grabBlur", NoZoneGain = "noZoneGain", BumpMean = "bumpMean";

        /// <summary>Every key, its start value and range. The grades of the start values are WmvLiquidMaterials'.</summary>
        public static readonly LiquidHighKey[] Keys =
        {
            new LiquidHighKey(Refract, 0.02, 0.0, 1.0),           // screen-uv offset per unit of the ripple normal's horizontal part
            new LiquidHighKey(RefractRamp, 0.3, 1e-3, 100.0),     // yd of Ls over which that offset reaches full
            new LiquidHighKey(TFastA, 1.09, 0.0, 10.0),           // transmission, fast term: amplitude
            new LiquidHighKey(TFastL, 0.154, 1e-4, 1000.0),       //   and length (yd)
            new LiquidHighKey(TSlowA, 0.38, 0.0, 10.0),           // transmission, slow term: amplitude
            new LiquidHighKey(TSlowL, 24.9, 1e-4, 1000.0),        //   and length (yd)
            new LiquidHighKey(WaterGain, 1.35, 0.0, 10.0),        // the water colour's gain on the zone light's colours
            new LiquidHighKey(NoZoneGain, 0.3125, 0.0, 10.0),     // without a zone light: on the type's MinimapStaticCol (saturated)
            new LiquidHighKey(ReflBroad, 0.2, 0.0, 1.0),          // broad (blurred) reflection: gain on Schlick
            new LiquidHighKey(ReflDetail, 0.02, 0.0, 1.0),        // detail (sharp) reflection: gain on Schlick
            new LiquidHighKey(F0, 0.02, 0.0, 1.0),                // Schlick's F0
            new LiquidHighKey(ReflBump, 0.25, 0.0, 1.0),          // the share of the ripple normal that tilts the reflected ray
            new LiquidHighKey(ReflMaxDist, 128.0, 1.0, 3000.0),   // the reflection march's length (yd)
            new LiquidHighKey(ReflThick, 0.3, 0.0, 100.0),        // its hit thickness (yd)
            new LiquidHighKey(ReflThickPct, 0.02, 0.0, 1.0),      //   plus this share of the scene point's distance
            new LiquidHighKey(ReflEdge, 0.2, 1e-4, 0.5),          // the hit's fade towards the screen edges (share of the screen)
            new LiquidHighKey(BumpTileA, 6.0, 0.01, 1000.0),      // ripple scale A: yd per repeat of the ripple map
            new LiquidHighKey(BumpTileB, 20.0, 0.01, 1000.0),     // ripple scale B
            new LiquidHighKey(BumpScroll, 0.05, -100.0, 100.0),   // ripple scroll (yd/s; A and B in opposite directions)
            new LiquidHighKey(BumpWeightA, 0.4, 0.0, 1.0),        // A's weight (B: 1 - A)
            new LiquidHighKey(BumpStrength, 2.0, 0.0, 10.0),      // the ripple slope's scale
            new LiquidHighKey(BumpMean, 1.0, 0.0, 1.0),           // the share of the ripple map's mean tilt taken out (1 all, 0 none)
            new LiquidHighKey(SkyR, 0.50, 0.0, 4.0),              // the sky reflection misses take without a zone light's sky
            new LiquidHighKey(SkyG, 0.60, 0.0, 4.0),              //   (authored rgb)
            new LiquidHighKey(SkyB, 0.70, 0.0, 4.0),
            new LiquidHighKey(GrabBlur, 1.0, 0.0, 16.0),          // the broad reflection's blur: tap spacing in 1/4-resolution texels
        };

        readonly Dictionary<string, double> values = new Dictionary<string, double>(StringComparer.OrdinalIgnoreCase);
        /// <summary>The keys overridden (in the order first given, the key's own spelling) and the tokens ignored, with why.</summary>
        public readonly List<string> Overridden = new List<string>();
        public readonly List<string> Ignored = new List<string>();

        public LiquidHighTune()
        {
            foreach (LiquidHighKey k in Keys)
                values[k.Key] = k.Start;
        }

        public static LiquidHighKey Find(string key)
        {
            if (key == null)
                return null;
            foreach (LiquidHighKey k in Keys)
                if (string.Equals(k.Key, key, StringComparison.OrdinalIgnoreCase))
                    return k;
            return null;
        }

        /// <summary>The value of `key` (the start value unless overridden); an unknown key throws.</summary>
        public double this[string key]
        {
            get
            {
                double v;
                if (!values.TryGetValue(key, out v))
                    throw new ArgumentException("no water tune key '" + key + "'", "key");
                return v;
            }
        }

        public bool IsOverridden(string key)
        {
            LiquidHighKey k = Find(key);
            return k != null && Overridden.Contains(k.Key);
        }

        /// <summary>The start values overridden by `text` ("key:value" pairs separated by commas, semicolons or white space;
        /// null or empty: none).</summary>
        public static LiquidHighTune Parse(string text)
        {
            var t = new LiquidHighTune();
            if (string.IsNullOrEmpty(text))
                return t;
            foreach (string token in text.Split(new[] { ',', ';', ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries))
                t.Apply(token);
            return t;
        }

        /// <summary>The -wmvWaterTune option's text in `args` (the command line's tokens, then WMV_DEBUG's from `envFirst` on:
        /// WMV_DEBUG is split on commas, so its pairs arrive as tokens of their own), or null when no token passes it. A
        /// token "-wmvWaterTune=..." adds what follows the '=' (when anything does); every token after it that does not
        /// start with '-' is one more pair, up to the next '-' token or the start of WMV_DEBUG's tokens. Given twice, the
        /// pairs add up (a key's last value wins in Parse). The pieces are joined with commas.</summary>
        public static string TuneTokens(IList<string> args, int envFirst)
        {
            const string option = "-wmvWaterTune=";
            if (args == null)
                return null;
            List<string> pairs = null;
            bool inTune = false;
            for (int i = 0; i < args.Count; i++)
            {
                string a = args[i] ?? "";
                if (i == envFirst)
                    inTune = false;
                if (inTune && !a.StartsWith("-", StringComparison.Ordinal))
                {
                    if (a.Length > 0)
                        pairs.Add(a);
                    continue;
                }
                inTune = false;
                if (a.StartsWith(option, StringComparison.Ordinal))
                {
                    if (pairs == null)
                        pairs = new List<string>();
                    string v = a.Substring(option.Length);
                    if (v.Length > 0)
                        pairs.Add(v);
                    inTune = true;
                }
            }
            return pairs != null ? string.Join(",", pairs.ToArray()) : null;
        }

        /// <summary>One "key:value" token; false (and said in Ignored) when it is not one, names no key, or its value is not a
        /// finite number within the key's range.</summary>
        public bool Apply(string token)
        {
            int colon = token != null ? token.IndexOf(':') : -1;
            if (colon <= 0 || colon == token.Length - 1)
            {
                Ignored.Add((token ?? "") + " (not key:value)");
                return false;
            }
            string key = token.Substring(0, colon), text = token.Substring(colon + 1);
            LiquidHighKey k = Find(key);
            if (k == null)
            {
                Ignored.Add(token + " (unknown key)");
                return false;
            }
            double v;
            if (!double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out v) || double.IsNaN(v) || double.IsInfinity(v))
            {
                Ignored.Add(token + " (not a number)");
                return false;
            }
            if (v < k.Min || v > k.Max)
            {
                Ignored.Add(token + string.Format(CultureInfo.InvariantCulture, " (outside {0}..{1})", k.Min, k.Max));
                return false;
            }
            values[k.Key] = v;
            if (!Overridden.Contains(k.Key))
                Overridden.Add(k.Key);
            return true;
        }

        /// <summary>The tune in a line (the log): the overridden values, or none, and what was ignored.</summary>
        public string Describe()
        {
            var parts = new List<string>();
            foreach (string key in Overridden)
                parts.Add(key + "=" + values[key].ToString("R", CultureInfo.InvariantCulture));
            string s = parts.Count > 0 ? string.Join(", ", parts.ToArray()) + " (" + parts.Count + " overridden; every other value its start value)"
                                       : "none (every value its start value)";
            if (Ignored.Count > 0)
                s += "; ignored: " + string.Join(", ", Ignored.ToArray());
            return s;
        }
    }

    /// <summary>A ripple map's check: is it a unit normal map (rgb = (n + 1) / 2 with |n| close to 1)?</summary>
    public static class LiquidBump
    {
        /// <summary>|n| must lie within MinLength..MaxLength for at least MinShare of the texels sampled.</summary>
        public const double MinLength = 0.9, MaxLength = 1.1, MinShare = 0.95;
        /// <summary>The texels sampled at most per side (a regular grid over the top mip).</summary>
        public const int SamplesPerSide = 64;

        /// <summary>The share of the sampled texels of `rgba` (width x height, 4 bytes a texel, rows as stored) whose normal
        /// (r, g, b) / 255 x 2 - 1 has a length within MinLength..MaxLength; `sampled` the texels looked at (0: no data).</summary>
        public static double UnitShare(byte[] rgba, int width, int height, out int sampled)
        {
            sampled = 0;
            if (rgba == null || width <= 0 || height <= 0 || rgba.Length < 4L * width * height)
                return 0.0;
            int sx = Math.Max(1, width / SamplesPerSide), sy = Math.Max(1, height / SamplesPerSide);
            int unit = 0;
            for (int y = sy / 2; y < height; y += sy)
                for (int x = sx / 2; x < width; x += sx)
                {
                    int at = 4 * (y * width + x);
                    double nx = rgba[at] / 255.0 * 2.0 - 1.0, ny = rgba[at + 1] / 255.0 * 2.0 - 1.0, nz = rgba[at + 2] / 255.0 * 2.0 - 1.0;
                    double len = Math.Sqrt(nx * nx + ny * ny + nz * nz);
                    if (len >= MinLength && len <= MaxLength)
                        unit++;
                    sampled++;
                }
            return sampled > 0 ? unit / (double)sampled : 0.0;
        }

        /// <summary>Is `rgba` a unit normal map (UnitShare at least MinShare)?</summary>
        public static bool IsUnitNormalMap(byte[] rgba, int width, int height, out double share, out int sampled)
        {
            share = UnitShare(rgba, width, height, out sampled);
            return sampled > 0 && share >= MinShare;
        }

        /// <summary>The slope spread the strength was tuned on: the client's newoceanbump.blp (FileDataID 463849) on this
        /// grid, sqrt(var(x / z) + var(y / z)) = 0.0957 (x 0.0530, y 0.0796; MEASURED offline on the decoded top mip with
        /// SlopeSpread's own sampling: the slopes' spread about their mean, the map's constant tilt left out).</summary>
        public const double TunedSlopeSpread = 0.0957;
        /// <summary>The largest strength factor (a gentler map is scaled up at most this much: a near-flat map's byte noise is
        /// not amplified). A documented choice.</summary>
        public const double MaxStrengthFactor = 2.0;

        /// <summary>The ripple map's slope spread on the UnitShare grid: sqrt(var(nx / nz) + var(ny / nz)) over the sampled
        /// texels whose |n| is within MinLength..MaxLength and whose nz is above 0.05 (population variances); `used` the
        /// texels that counted (0: none, the spread 0).</summary>
        public static double SlopeSpread(byte[] rgba, int width, int height, out int used)
        {
            double meanX, meanY;
            return SlopeSpread(rgba, width, height, out used, out meanX, out meanY);
        }

        /// <summary>SlopeSpread, and the slopes' mean over the same texels, (meanX, meanY) = (mean(nx / nz), mean(ny / nz)): the
        /// map's constant tilt, which the shader takes out of every sample's slope before the strength scale (_HiBumpMean;
        /// LiquidHighMath.RemoveTilt) -- (0, 0) when no texel counts. A flat map stored as (128, 128, 255) has the bytes' own
        /// 1 / 255.</summary>
        public static double SlopeSpread(byte[] rgba, int width, int height, out int used, out double meanX, out double meanY)
        {
            used = 0;
            meanX = 0.0;
            meanY = 0.0;
            if (rgba == null || width <= 0 || height <= 0 || rgba.Length < 4L * width * height)
                return 0.0;
            int sx = Math.Max(1, width / SamplesPerSide), sy = Math.Max(1, height / SamplesPerSide);
            double sumX = 0.0, sumY = 0.0, sumXX = 0.0, sumYY = 0.0;
            for (int y = sy / 2; y < height; y += sy)
                for (int x = sx / 2; x < width; x += sx)
                {
                    int at = 4 * (y * width + x);
                    double nx = rgba[at] / 255.0 * 2.0 - 1.0, ny = rgba[at + 1] / 255.0 * 2.0 - 1.0, nz = rgba[at + 2] / 255.0 * 2.0 - 1.0;
                    double len = Math.Sqrt(nx * nx + ny * ny + nz * nz);
                    if (len < MinLength || len > MaxLength || nz <= 0.05)
                        continue;
                    double a = nx / nz, b = ny / nz;
                    sumX += a;
                    sumY += b;
                    sumXX += a * a;
                    sumYY += b * b;
                    used++;
                }
            if (used == 0)
                return 0.0;
            double mx = sumX / used, my = sumY / used;
            meanX = mx;
            meanY = my;
            double v = Math.Max(0.0, sumXX / used - mx * mx) + Math.Max(0.0, sumYY / used - my * my);
            return Math.Sqrt(v);
        }

        /// <summary>The factor on the ripple strength that brings a map's slope spread to the tuned map's: TunedSlopeSpread /
        /// spread, at most MaxStrengthFactor; 1 for a flat map (spread below 1e-6). A steeper map (the client's oceanbump.blp,
        /// about 0.46) is scaled down, a gentler one (mercurybump.blp, about 0.075) up.</summary>
        public static double StrengthFactor(double spread)
        {
            if (double.IsNaN(spread) || spread < 1e-6)
                return 1.0;
            return Math.Min(MaxStrengthFactor, TunedSlopeSpread / spread);
        }
    }
}
