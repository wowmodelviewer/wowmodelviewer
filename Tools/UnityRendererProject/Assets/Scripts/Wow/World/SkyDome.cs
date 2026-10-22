// SkyDome.cs
//
// THE SKY'S COLOUR DOME (Stage 10 step 4, docs/world-viewer/10-light-sky-fog.md 4.0, 4.3, 6.8): the mesh the player draws
// behind the world and the client's sky pixel program on it, in pure C# (no UnityEngine). WmvWorldSky builds its Unity mesh
// from Build and gives Resources/WmvSky.shader the values Inputs makes; the lifecycle self-test compares the GPU with Trace
// (the CPU twin); Tests/SkyTerrainLightTests.cs checks the construction, the day curve, the inputs and the twin.
//
//   the dome     a top pole, five rings of 24 vertices at 15-deg longitude steps and a bottom pole (122 vertices, 240
//                triangles: a fan from the top pole to ring 1, quad strips between consecutive rings, a fan from ring 5 to the
//                bottom pole), in the TABLE's own object units -- the sky program's horizon ramp works in them (PROVEN_BYTES,
//                2977085: sat(50 z)) --, Unity y up (the table's z). The rings are one cubic construction (INFERRED (public):
//                the public ring table of the modern renderer, "table A"): at latitude l the radius |P(l - 0.5)| and the
//                height P(l) - cos(pi/4), P the cubic 1 - (6 - 4 f) f^2 of each half period with alternating sign, at the
//                latitudes 0 (the top pole), 0.17, 0.20, 0.23, 0.235, 0.25 and 1 (the bottom pole). Table A departs from the
//                construction twice: ring 4 at latitude 0.235 (0.24 in the older public table, "table B") and ring 5's height
//                -0.005 (table B: the construction's). Rings 1-3 fit the Stage 09 PTR shots to about 0.3 deg (MEASURED, step
//                1); rings 4 and 5 differ between the tables below 2.16 deg only and stay open (test T1; -wmvSkyTable=b draws
//                table B). Where longitude 0 lies is UNKNOWN: at most 0.13 deg of a ring's elevation between a vertex and the
//                middle of a facet.
//   the colours  per vertex (the sky program's COLOR, interpolated along the triangles): SkyTopColor at the top pole,
//                SkyMiddleColor, SkyBand1Color, SkyBand2Color and SkySmogColor on rings 1-4, and on ring 5 and the bottom pole
//                SkyFogColor leaning to EndFogColor by sat(game far clip / EndFogColorDistance) -- a zero EndFogColor keeps
//                SkyFogColor, a distance of 0 or less takes EndFogColor (INFERRED (public), table A's renderer, as the step-1
//                measurement evaluated it; -wmvSkyEndFog=off keeps SkyFogColor). Colours are the table's byte / 255
//                (MEASURED: no conversion constant).
//   the pixel    (PROVEN_BYTES, 2977085) rgb = v + sat(x^3) mix(wBelow, wAbove, sat(50 z)) (C - v), with
//                x = sat(dot(normalize(dir), normalize(sun)) - a) / (1 - a), applied where x > 0; v the interpolated colour,
//                dir the dome position (the program's o2.xyz: INFERRED to be the identity for a world-aligned dome), z the RAW
//                table height (o2.w); no dither (cb1[3].x 0: off by choice, the client's setting UNKNOWN). The inputs
//                (INFERRED (public)): a = SunFogAngle (a zero
//                SunFogColor sets a = 1: no tint), C = SunFogColor, wAbove = wBelow = SunAngleBlend (1) x SunFogStrength x
//                the day curve (0 before 06:30 and after 18:30, linear ramps over 06:30-07:00 and 18:00-18:30, 1 between; on
//                the light's half-minute, as the step-1 measurement evaluated it), sun = the sun direction (WorldLightModel's
//                sun path). The client's separate below-horizon strength is UNKNOWN: one value serves both.
// Thread safety: no mutable static state.

using System;

namespace Wmv.Wow.World
{
    /// <summary>The sky's inputs for one frame (see the file header). Colours authored 0..1.</summary>
    public sealed class SkyInputs
    {
        /// <summary>The vertex colours, r g b each, by colour slot (SkyDome.SlotOf): the top pole (SkyTop), ring 1
        /// (SkyMiddle), ring 2 (SkyBand1), ring 3 (SkyBand2), ring 4 (SkySmog), ring 5 and the bottom pole (SkyFog, or its
        /// lean to EndFog).</summary>
        public readonly double[] Colours = new double[3 * SkyDome.ColourSlots];
        /// <summary>The tint's angle a; 1 or more: no tint.</summary>
        public double Angle = 1.0;
        /// <summary>The tint's colour C (SunFogColor).</summary>
        public readonly double[] Tint = new double[3];
        /// <summary>The tint's strength above and below the horizon ramp (one value: the client's second is UNKNOWN).</summary>
        public double WeightAbove, WeightBelow;
        /// <summary>The day curve's value and EndFogColor's share in the ring-5 colour (for the log and the tests).</summary>
        public double DayCurve, FogLean;
        /// <summary>Towards the sun, in the frame the dome is built in (Unity's: y up).</summary>
        public readonly double[] Sun = new double[3];

        public void CopyTo(SkyInputs o)
        {
            Array.Copy(Colours, o.Colours, Colours.Length);
            Array.Copy(Tint, o.Tint, Tint.Length);
            Array.Copy(Sun, o.Sun, Sun.Length);
            o.Angle = Angle;
            o.WeightAbove = WeightAbove;
            o.WeightBelow = WeightBelow;
            o.DayCurve = DayCurve;
            o.FogLean = FogLean;
        }
    }

    public static class SkyDome
    {
        public const int Segments = 24;                 // vertices per ring, 15-deg steps (INFERRED (public))
        public const int RingCount = 5;
        public const int ProfilePoints = RingCount + 2; // the top pole, rings 1-5, the bottom pole
        public const int ColourSlots = 6;
        public const int VertexCount = 2 + RingCount * Segments;                          // 122
        public const int TriangleCount = 2 * Segments + 2 * Segments * (RingCount - 1);   // 240

        /// <summary>The construction's latitudes of the profile points (see the file header): table A and table B.</summary>
        public static readonly double[] LatitudesA = { 0.0, 0.17, 0.20, 0.23, 0.235, 0.25, 1.0 };
        public static readonly double[] LatitudesB = { 0.0, 0.17, 0.20, 0.23, 0.24, 0.25, 1.0 };
        /// <summary>Table A's ring-5 height, its one value the construction does not give (INFERRED (public)).</summary>
        public const double RingFiveHeightA = -0.005;
        /// <summary>The colour slot of each profile point (ring 5 and the bottom pole share the fog colour).</summary>
        public static readonly int[] SlotOf = { 0, 1, 2, 3, 4, 5, 5 };
        /// <summary>Each colour slot's light column (Inputs reads them; never written).</summary>
        static readonly int[] SlotColumns =
        {
            WorldLightColumns.SkyTopColor, WorldLightColumns.SkyMiddleColor, WorldLightColumns.SkyBand1Color, WorldLightColumns.SkyBand2Color,
            WorldLightColumns.SkySmogColor, WorldLightColumns.SkyFogColor,
        };
        public const string TableGrade = "INFERRED (public)";
        public const string RingsOneToThreeGrade = "MEASURED";          // to about 0.3 deg (step 1)
        public const string RingsFourFiveGrade = "INFERRED (public)";   // open: test T1

        /// <summary>The sky program's horizon ramp: sat(50 z) of the raw table height (PROVEN_BYTES, 2977085).</summary>
        public const double HorizonRamp = 50.0;
        public const string HorizonRampGrade = "PROVEN_BYTES";
        /// <summary>SunAngleBlend, the strength's other factor (INFERRED (public): 1).</summary>
        public const double SunAngleBlend = 1.0;
        public const string SunAngleBlendGrade = "INFERRED (public)";
        /// <summary>The day curve's keys, minutes into the day: 0 before 06:30, a linear ramp to 1 at 07:00, 1 until 18:00, a
        /// linear ramp to 0 at 18:30 (INFERRED (public)).</summary>
        public static readonly double[] DayCurveMinutes = { 390.0, 420.0, 1080.0, 1110.0 };
        public const string DayCurveGrade = "INFERRED (public)";
        public const string FogLeanGrade = "INFERRED (public)";

        /// <summary>The construction's cubic: on each half period f in [0, 1) of the phase, 1 - (6 - 4 f) f^2, the sign
        /// alternating with the period (a cubic stand-in for cos(pi phase)).</summary>
        public static double Cubic(double phase)
        {
            int period = phase <= 0.0 ? (int)phase - 1 : (int)phase;
            double f = phase - period;
            double v = 1.0 - (6.0 - 4.0 * f) * f * f;
            return (period & 1) != 0 ? -v : v;
        }

        /// <summary>The seven profile points (radius, height; the poles have radius 0) of table A or B.</summary>
        public static void Profile(bool tableB, double[] radius, double[] height)
        {
            double[] lat = tableB ? LatitudesB : LatitudesA;
            double c45 = Math.Cos(Math.PI / 4.0);
            for (int k = 0; k < ProfilePoints; k++)
            {
                double l = lat[k];
                radius[k] = l > 0.0 && l < 1.0 ? Math.Abs(Cubic(l - 0.5)) : 0.0;
                height[k] = Cubic(l) - c45;
            }
            if (!tableB)
                height[5] = RingFiveHeightA;
        }

        /// <summary>A profile point's elevation (degrees) seen from the dome's centre.</summary>
        public static double ElevationDeg(double radius, double height)
        {
            if (radius > 0.0)
                return Math.Atan2(height, radius) * 180.0 / Math.PI;
            return height >= 0.0 ? 90.0 : -90.0;
        }

        /// <summary>The dome's vertices (3 x VertexCount floats: x, y up, z), each vertex's colour slot (VertexCount) and the
        /// triangles (3 x TriangleCount indices). Vertex 0 is the top pole, then ring 1's 24 (longitude 15 j deg, from +x
        /// towards +z), ... ring 5's, and the bottom pole last.</summary>
        public static void Build(bool tableB, float[] positions, float[] slots, int[] indices)
        {
            if (positions == null || positions.Length < 3 * VertexCount || slots == null || slots.Length < VertexCount || indices == null ||
                indices.Length < 3 * TriangleCount)
                throw new ArgumentException("the dome needs " + VertexCount + " vertices and " + TriangleCount + " triangles");
            var r = new double[ProfilePoints];
            var h = new double[ProfilePoints];
            Profile(tableB, r, h);
            int v = 0;
            Put(positions, slots, v++, 0.0, h[0], 0.0, SlotOf[0]);
            for (int ring = 1; ring <= RingCount; ring++)
                for (int j = 0; j < Segments; j++)
                {
                    double lon = j * (2.0 * Math.PI / Segments);
                    Put(positions, slots, v++, r[ring] * Math.Cos(lon), h[ring], r[ring] * Math.Sin(lon), SlotOf[ring]);
                }
            Put(positions, slots, v++, 0.0, h[ProfilePoints - 1], 0.0, SlotOf[ProfilePoints - 1]);
            int t = 0;
            int bottom = VertexCount - 1;
            for (int j = 0; j < Segments; j++)
            {
                int j1 = (j + 1) % Segments;
                indices[t++] = 0;
                indices[t++] = Ring(1, j);
                indices[t++] = Ring(1, j1);
            }
            for (int ring = 1; ring < RingCount; ring++)
                for (int j = 0; j < Segments; j++)
                {
                    int j1 = (j + 1) % Segments;
                    int a = Ring(ring, j), b = Ring(ring, j1), c = Ring(ring + 1, j), d = Ring(ring + 1, j1);
                    indices[t++] = a; indices[t++] = c; indices[t++] = b;
                    indices[t++] = b; indices[t++] = c; indices[t++] = d;
                }
            for (int j = 0; j < Segments; j++)
            {
                int j1 = (j + 1) % Segments;
                indices[t++] = Ring(RingCount, j);
                indices[t++] = bottom;
                indices[t++] = Ring(RingCount, j1);
            }
        }

        static int Ring(int ring, int j) { return 1 + (ring - 1) * Segments + j; }

        static void Put(float[] p, float[] s, int v, double x, double y, double z, int slot)
        {
            p[3 * v] = (float)x;
            p[3 * v + 1] = (float)y;
            p[3 * v + 2] = (float)z;
            s[v] = slot;
        }

        static double Sat(double x) { return x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : x; }

        /// <summary>The day curve (see the file header) at a half-minute of the day.</summary>
        public static double DayCurve(int halfMinute)
        {
            double m = halfMinute / 2.0;
            double[] k = DayCurveMinutes;
            if (m < k[0] || m > k[3])
                return 0.0;
            if (m < k[1])
                return (m - k[0]) / (k[1] - k[0]);
            if (m > k[2])
                return (k[3] - m) / (k[3] - k[2]);
            return 1.0;
        }

        /// <summary>EndFogColor's share in the ring-5 colour: sat(farClip / distance), 1 for a distance of 0 or less.</summary>
        public static double FogLean(double farClip, double endFogDistance)
        {
            return endFogDistance > 0.0 ? Sat(farClip / endFogDistance) : 1.0;
        }

        /// <summary>The frame's inputs from a light state's channels (WorldLightSample's layout: the eased WorldLightEase.V or
        /// a sample's V) at `halfMinute`, with the game far clip; `endFog` false keeps SkyFogColor on ring 5 (-wmvSkyEndFog=off).
        /// The sun is the caller's (s.Sun, in the dome's frame).</summary>
        public static void Inputs(double[] v, int halfMinute, double farClip, bool endFog, SkyInputs s)
        {
            for (int k = 0; k < ColourSlots; k++)
            {
                int at = WorldLightSample.ColourAt(SlotColumns[k]);
                s.Colours[3 * k] = v[at];
                s.Colours[3 * k + 1] = v[at + 1];
                s.Colours[3 * k + 2] = v[at + 2];
            }
            int e = WorldLightSample.ColourAt(WorldLightColumns.EndFogColor);
            bool endFogZero = v[e] == 0.0 && v[e + 1] == 0.0 && v[e + 2] == 0.0;
            s.FogLean = 0.0;
            if (endFog && !endFogZero)
            {
                double w = FogLean(farClip, v[WorldLightSample.FloatAt(WorldLightColumns.EndFogColorDistance)]);
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
            s.DayCurve = DayCurve(halfMinute);
            double w2 = SunAngleBlend * v[WorldLightSample.FloatAt(WorldLightColumns.SunFogStrength)] * s.DayCurve;
            s.WeightAbove = w2;
            s.WeightBelow = w2;
        }

        /// <summary>The sky program (see the file header) on an interpolated colour v, dome position (dx, dy, dz) and raw table
        /// height z.</summary>
        public static void Shade(double vr, double vg, double vb, double dx, double dy, double dz, double z, SkyInputs s,
                                 out double r, out double g, out double b)
        {
            r = vr;
            g = vg;
            b = vb;
            double a = s.Angle;
            if (!(a < 1.0))
                return;
            double dl = Math.Sqrt(dx * dx + dy * dy + dz * dz);
            double sx = s.Sun[0], sy = s.Sun[1], sz = s.Sun[2];
            double sl = Math.Sqrt(sx * sx + sy * sy + sz * sz);
            if (!(dl > 0.0) || !(sl > 0.0))
                return;
            double x = Sat((dx * sx + dy * sy + dz * sz) / (dl * sl) - a) / (1.0 - a);
            if (!(x > 0.0))
                return;
            double ramp = Sat(HorizonRamp * z);
            double k = Sat(x * x * x) * (s.WeightBelow + (s.WeightAbove - s.WeightBelow) * ramp);
            r = vr + k * (s.Tint[0] - vr);
            g = vg + k * (s.Tint[1] - vg);
            b = vb + k * (s.Tint[2] - vb);
        }

        /// <summary>The CPU twin of the GPU's draw: the ray from the dome's centre along (dx, dy, dz) meets one of the dome's
        /// triangles (Build's arrays); the colour, position and height there interpolated by its barycentric weights -- what
        /// the GPU's perspective-correct interpolation gives -- then Shade. False when no triangle is met (never for a
        /// direction of finite length).</summary>
        public static bool Trace(float[] positions, float[] slots, int[] indices, double dx, double dy, double dz, SkyInputs s,
                                 out double r, out double g, out double b)
        {
            r = g = b = 0.0;
            int best = -1;
            double bestT = double.PositiveInfinity, bu = 0.0, bv = 0.0;
            for (int t = 0; t < TriangleCount; t++)
            {
                int i0 = indices[3 * t], i1 = indices[3 * t + 1], i2 = indices[3 * t + 2];
                double p0x = positions[3 * i0], p0y = positions[3 * i0 + 1], p0z = positions[3 * i0 + 2];
                double e1x = positions[3 * i1] - p0x, e1y = positions[3 * i1 + 1] - p0y, e1z = positions[3 * i1 + 2] - p0z;
                double e2x = positions[3 * i2] - p0x, e2y = positions[3 * i2 + 1] - p0y, e2z = positions[3 * i2 + 2] - p0z;
                double px = dy * e2z - dz * e2y, py = dz * e2x - dx * e2z, pz = dx * e2y - dy * e2x;
                double det = e1x * px + e1y * py + e1z * pz;
                if (Math.Abs(det) < 1e-15)
                    continue;
                double inv = 1.0 / det;
                double tx = -p0x, ty = -p0y, tz = -p0z;          // the ray's origin (0, 0, 0) minus p0
                double u = (tx * px + ty * py + tz * pz) * inv;
                if (u < -1e-9 || u > 1.0 + 1e-9)
                    continue;
                double qx = ty * e1z - tz * e1y, qy = tz * e1x - tx * e1z, qz = tx * e1y - ty * e1x;
                double w = (dx * qx + dy * qy + dz * qz) * inv;
                if (w < -1e-9 || u + w > 1.0 + 1e-9)
                    continue;
                double dist = (e2x * qx + e2y * qy + e2z * qz) * inv;
                if (dist <= 0.0 || dist >= bestT)
                    continue;
                bestT = dist;
                best = t;
                bu = u;
                bv = w;
            }
            if (best < 0)
                return false;
            int a0 = indices[3 * best], a1 = indices[3 * best + 1], a2 = indices[3 * best + 2];
            double w0 = 1.0 - bu - bv, w1 = bu, w2 = bv;
            double hx = 0.0, hy = 0.0, hz = 0.0, cr = 0.0, cg = 0.0, cb = 0.0;
            int[] vs = { a0, a1, a2 };
            double[] ws = { w0, w1, w2 };
            for (int k = 0; k < 3; k++)
            {
                int vi = vs[k];
                double wk = ws[k];
                hx += wk * positions[3 * vi];
                hy += wk * positions[3 * vi + 1];
                hz += wk * positions[3 * vi + 2];
                int slot = (int)slots[vi];
                cr += wk * s.Colours[3 * slot];
                cg += wk * s.Colours[3 * slot + 1];
                cb += wk * s.Colours[3 * slot + 2];
            }
            Shade(cr, cg, cb, hx, hy, hz, hy, s, out r, out g, out b);
            return true;
        }
    }
}
