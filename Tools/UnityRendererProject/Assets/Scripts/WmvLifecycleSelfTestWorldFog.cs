// WmvLifecycleSelfTestWorldFog.cs
//
// The lifecycle self-test's FOG (Stage 10 step 7, docs/world-viewer/10-light-sky-fog.md 6.9; Wow/World/WorldFog.cs is the CPU
// twin and the evidence, Resources/WmvWorldFog.hlsl the GPU's): OutdoorFogTests, called by OutdoorLightTests after the world
// models' and doodads' tests (F16, the hard off, is in OutdoorLightHardOffTests). The GPU cases draw a probe inside
// WmvTerrainComposite's render in float32 (_WmvWLOn pinned 0 by the composite unless the probe's own command buffer sets it,
// _WmvShaderEncode 0) over a window of 1,600 yd on 16 x 16 pixels: a planar quad rising from 20 yd at the west edge to 140 at
// the east, the eye north-west of the window through _WmvWLEye, so every pixel centre has a known position and distance
// (about 460 to 2,550 yd). Each pixel is compared with the CPU twin -- TerrainOutdoorLight.Shade or ModelOutdoorLight, then
// WorldFog.Apply -- within 5e-4, and the worst is reported:
//   F1  the terrain, exponential: start, density, end fade, the colour ramp (c^3 and c), the height factor between 0 and 1,
//       the sun tint towards the window, the pull; and a record with R5.x = 1 (no tint: the state of most Retail keys)
//   F2  the terrain, power path; F3 the terrain, polynomial path mixed in by 0.35 (the client's own fixture coefficients)
//   F4  visibility 1 is exact: a start beyond the window draws the fog-off render bit for bit; F4b also with a fog colour of
//       +infinity (the output select returns the input; the mix alone would give NaN); F5 the terrain's fog slot 0
//   F6  M2 opaque and alpha-keyed; F7 the M2 fog targets 1-4 (alpha checked); F8 NoFog, unlit, the albedo view
//   F9  the world model: lit, F_UNLIT, F_UNFOGGED; F10 the variants' fog slot 0 (F10b: F4b's record on both variants); F11
//       the originals read no fog global
//   F12 the per-copy fog vectors and the binding line's fog counts through the real managers (a synthetic M2 of the nine
//       blend modes with one NoFog batch as a doodad; a synthetic world model with an F_UNFOGGED and a MOMT blend 2 entry)
//   F13 the driver (an object with every fog column sent): the published rows are WorldFog.Pack's (the far clip and the
//       horizonStart the driver holds), R2.w and the eye follow SetEye within one frame, a product the fog is off for writes no
//       fog global, a null set, the census once per set, the capture line's fog clause and the state line's end E = min(0.933 x
//       far clip, horizonStart); an object whose fog columns are not sent (the light tests' own) draws no fog and says why
//   F14 the composite's pin: no light and no fog in a fixture whatever the fog globals hold
//   F15 no allocation in Publish (where the runtime counts allocations per thread; otherwise SKIP)
// Every global touched is saved and put back, and the driver is put back to the switches' start.

using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;
using Wmv.Wow;
using Wmv.Wow.World;

public static partial class WmvLifecycleSelfTest
{
    const float FogWindow = 1600f;              // yd, both ways: x 0 .. 1600 (west to east), z 0 .. -1600 (north to south)
    const int FogPixels = 16;
    const float FogHeightWest = 20f, FogHeightEast = 140f;
    const double FogGate = 5e-4;                // float32 renders (expected below 1e-4)
    static readonly Vector3 FogEye = new Vector3(-300f, 60f, 250f);
    static readonly double[] FogUp = { 0.0, 1.0, 0.0 };

    /// <summary>A record of 56 values (14 rows, x y z w), each a float as the GPU gets it.</summary>
    static WorldFogRecord FogRecordOf(float[] rows)
    {
        var r = new WorldFogRecord { Mode = WorldFogMode.RetailPattern };
        for (int k = 0; k < r.R.Length; k++)
            r.R[k] = rows[k];
        return r;
    }

    /// <summary>A copy of `b` with value `at` (4 row + component) set to `value`.</summary>
    static WorldFogRecord FogRecordWith(WorldFogRecord b, int at, float value)
    {
        var r = new WorldFogRecord();
        b.CopyTo(r);
        r.R[at] = value;
        return r;
    }

    /// <summary>The probes' base record (F1): a start of 500 yd (the nearest pixels before it), k 0.0015, the end 2,600 (its fade
    /// from 780 yd); the height plane at Unity y 50 (R2.w = eye.y - 50) with rate 0.02 and dawn's LightParams 5985 height cubic,
    /// so hf runs from 0 to 1 across the rising quad, the height density 0.0005; the colour ramp from 800 yd over 900; the sun
    /// tint towards the window (a 0.2, strength 0.3); the pull m 40; no power path, no polynomial path.</summary>
    static WorldFogRecord FogBaseRecord()
    {
        Vector3 s = new Vector3(0.7f, 0.1f, -0.7f).normalized;
        return FogRecordOf(new[]
        {
            500f, 2600f, 0.0015f, 0f,
            0f, 0f, 0f, 0f,
            0f, 1f, 0f, FogEye.y - 50f,
            0.30f, 0.25f, 0.20f, 0.02f,
            0.0005f, 0.55f, 0.40f, 0.35f,
            0.2f, 0.90f, 0.65f, 0.45f,
            0.38f, 0.25f, 0.22f, 900f,
            0.3f, 0f, 0f, 0f,
            s.x, s.y, s.z, 40f,
            0.18041f, 0.39225f, 0.43267f, 0.00078f,
            0f, 0f, 0f, 0f,
            0f, 0f, 0f, 0f,
            0f, 0f, 1f, 1f,
            0.45f, 0.33f, 0.28f, 800f,
        });
    }

    /// <summary>The probe quad over the window: height FogHeightWest at x 0 rising to FogHeightEast at x 1600 (planar, so a pixel's
    /// interpolated position is the plane's at its centre), the vertex normal `n` and, in UV1, the terrain's light normal `n`;
    /// UV0 u = x / 1600 (the 2 x 1 probe texture: columns 0-7 texel 0, 8-15 texel 1).</summary>
    static Mesh FogProbeQuad(Vector3 n, List<UnityEngine.Object> made)
    {
        var quad = new Mesh { name = "WmvWorldFog probe quad" };
        made.Add(quad);
        quad.SetVertices(new List<Vector3>
        {
            new Vector3(0f, FogHeightWest, 0f), new Vector3(FogWindow, FogHeightEast, 0f), new Vector3(FogWindow, FogHeightEast, -FogWindow),
            new Vector3(0f, FogHeightWest, -FogWindow),
        });
        quad.SetNormals(new List<Vector3> { n, n, n, n });
        quad.SetUVs(0, new List<Vector2> { new Vector2(0f, 0f), new Vector2(1f, 0f), new Vector2(1f, 1f), new Vector2(0f, 1f) });
        quad.SetUVs(1, new List<Vector3> { n, n, n, n });
        quad.SetIndices(new[] { 0, 1, 2, 0, 2, 3 }, MeshTopology.Triangles, 0);
        quad.RecalculateBounds();
        return quad;
    }

    /// <summary>Pixel (column i, row j north first): its fog position, the pixel centre on the quad minus the eye.</summary>
    static double[] FogPixelP(int i, int j)
    {
        double x = (i + 0.5) * FogWindow / FogPixels, z = -(j + 0.5) * FogWindow / FogPixels;
        double y = FogHeightWest + (FogHeightEast - FogHeightWest) * x / FogWindow;
        return new[] { x - FogEye.x, y - FogEye.y, z - FogEye.z };
    }

    static void FogRows(CommandBuffer cmd, WorldFogRecord r)
    {
        for (int k = 0; k < WorldFogRecord.Rows; k++)
            cmd.SetGlobalVector(WmvWorldLight.FogRowIds[k], new Vector4((float)r.R[4 * k], (float)r.R[4 * k + 1], (float)r.R[4 * k + 2], (float)r.R[4 * k + 3]));
    }

    /// <summary>One fog probe: `quad` drawn with `m` inside WmvTerrainComposite's render (float32, 16 x 16 over the window). The
    /// probe's own command buffer sets, after the composite's pin, _WmvWLOn 1 when `on`, the light `l`, _WmvWLTerrain `terrain`,
    /// _WmvWLModels `models`, the record's rows (none when `r` is null), the eye and _WmvRig 0. The caller puts the globals back.
    /// Returns the 256 pixels' RGBA (rows north first).</summary>
    static float[] FogProbe(Material m, Mesh quad, bool on, ModelProbeLight l, Vector4 terrain, Vector4 models, WorldFogRecord r)
    {
        return WmvTerrainComposite.RenderToArray(cmd =>
        {
            if (on)
                cmd.SetGlobalFloat(WmvWorldLight.OnId, 1f);
            cmd.SetGlobalVector(WmvWorldLight.LightDirId, new Vector4(l.L.x, l.L.y, l.L.z, 0f));
            cmd.SetGlobalVector(WmvWorldLight.AmbientId, l.Ambient);
            cmd.SetGlobalVector(WmvWorldLight.HorizonAmbientId, l.Horizon);
            cmd.SetGlobalVector(WmvWorldLight.GroundAmbientId, l.Ground);
            cmd.SetGlobalVector(WmvWorldLight.DirectId, l.Direct);
            cmd.SetGlobalVector(WmvWorldLight.TerrainId, terrain);
            cmd.SetGlobalVector(WmvWorldLight.ModelsId, models);
            if (r != null)
                FogRows(cmd, r);
            cmd.SetGlobalVector(WmvWorldLight.EyeId, new Vector4(FogEye.x, FogEye.y, FogEye.z, 1f));
            cmd.SetGlobalFloat(RigId, 0f);
            cmd.DrawMesh(quad, Matrix4x4.identity, m, 0, 0);
        }, 0f, 0f, FogWindow, FogWindow, 0f, 200f, FogPixels, FogPixels, false, false, true);
    }

    /// <summary>The worst channel of a fog probe against the twin: each pixel's colour before the fog `lit(i)` (rgba; column i;
    /// null: not compared) fogged by `r` for `kind` (no record: as it is), alpha compared when `alpha`. `fogged` counts the
    /// compared pixels whose visibility is below 1, `atOne` those at 1 exactly; `finite` that every compared value is.</summary>
    static double FogProbeWorst(float[] px, Func<int, double[]> lit, WorldFogRecord r, int kind, bool alpha, out int fogged, out int atOne,
                                out bool finite)
    {
        double worst = 0.0;
        fogged = atOne = 0;
        finite = true;
        var o = new double[4];
        for (int j = 0; j < FogPixels; j++)
            for (int i = 0; i < FogPixels; i++)
            {
                double[] c = lit(i);
                if (c == null)
                    continue;
                double[] p = FogPixelP(i, j);
                if (r != null)
                {
                    WorldFog.Apply(r, p, FogUp, c, kind, o);
                    double vis, hf;
                    WorldFog.Visibility(r, p, FogUp, out vis, out hf);
                    if (vis < 1.0)
                        fogged++;
                    else
                        atOne++;
                }
                else
                    Array.Copy(c, o, 4);
                int at = 4 * (j * FogPixels + i);
                for (int k = 0; k < (alpha ? 4 : 3); k++)
                {
                    finite &= !float.IsNaN(px[at + k]) && !float.IsInfinity(px[at + k]);
                    worst = Math.Max(worst, Math.Abs(px[at + k] - o[k]));
                }
            }
        return worst;
    }

    static double FogProbeWorst(float[] px, Func<int, double[]> lit, WorldFogRecord r, int kind, bool alpha)
    {
        int f, a;
        bool finite;
        double w = FogProbeWorst(px, lit, r, kind, alpha, out f, out a, out finite);
        return finite ? w : double.PositiveInfinity;
    }

    /// <summary>Pixels of two renders exactly equal in all four channels (of 256).</summary>
    static int FogSame(float[] a, float[] b)
    {
        return ModelProbeSame(a, b);
    }

    static double[] Rgba(double[] rgb, double a) { return new[] { rgb[0], rgb[1], rgb[2], a }; }

    /// <summary>A worst difference as the fog cases report it (float32 renders: scientific, so a few ulps show).</summary>
    static string FE(double v) { return v.ToString("0.0E+0", System.Globalization.CultureInfo.InvariantCulture); }

    static void OutdoorFogTests(Action<string> log)
    {
        var ids = new List<int>
        {
            WmvWorldLight.TerrainId, WmvWorldLight.ModelsId, WmvWorldLight.LightDirId, WmvWorldLight.AmbientId, WmvWorldLight.HorizonAmbientId,
            WmvWorldLight.GroundAmbientId, WmvWorldLight.DirectId, WmvWorldLight.EyeId,
        };
        ids.AddRange(WmvWorldLight.FogRowIds);
        var saved = new Vector4[ids.Count];
        for (int k = 0; k < ids.Count; k++)
            saved[k] = Shader.GetGlobalVector(ids[k]);
        float savedOn = Shader.GetGlobalFloat(WmvWorldLight.OnId), savedRig = Shader.GetGlobalFloat(RigId);
        try
        {
            try { OutdoorFogShaderTests(log); }
            catch (Exception e) { Check(false, "outdoor fog: the shader tests threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log); }
            try { OutdoorFogBindingTests(log); }
            catch (Exception e) { Check(false, "outdoor fog: the binding tests threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log); }
            try { OutdoorFogDriverTests(log); }
            catch (Exception e) { Check(false, "outdoor fog: the driver tests threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log); }
        }
        finally
        {
            for (int k = 0; k < ids.Count; k++)
                Shader.SetGlobalVector(ids[k], saved[k]);
            Shader.SetGlobalFloat(WmvWorldLight.OnId, savedOn);
            Shader.SetGlobalFloat(RigId, savedRig);
        }
    }

    // ---------------------------------------------------------------- F1-F11, F14: the GPU against the twin

    static void OutdoorFogShaderTests(Action<string> log)
    {
        var made = new List<UnityEngine.Object>();
        try
        {
            Shader textured = WmvTerrainMaterials.ResolveShader(null);
            Shader m2Shader = WmvModelBuilder.ResolveShader(null), wmoShader = WmvWmoBuilder.ResolveMapObjectShader(null);
            Material probeM2 = m2Shader != null ? new Material(m2Shader) { name = "WmvWorldFog probe (M2 shader)" } : null;
            Material probeWmo = wmoShader != null ? new Material(wmoShader) { name = "WmvWorldFog probe (WMO shader)" } : null;
            if (probeM2 != null) made.Add(probeM2);
            if (probeWmo != null) made.Add(probeWmo);
            Shader m2Variant = probeM2 != null ? WmvWorldLitMaterials.ResolveVariant(true, probeM2, log) : null;
            Shader wmoVariant = probeWmo != null ? WmvWorldLitMaterials.ResolveVariant(false, probeWmo, log) : null;
            if (textured == null || m2Variant == null || wmoVariant == null)
            {
                Check(false, "outdoor fog: the probes' shaders -- Wmv/TerrainTextured " + (textured != null ? "found" : "MISSING") + ", the world-lit " +
                      "variants " + (m2Variant != null ? m2Variant.name : "MISSING") + " and " + (wmoVariant != null ? wmoVariant.name : "MISSING"), log);
                return;
            }
            var light = new ModelProbeLight(new Vector4(0.25f, 0.30f, 0.40f, 1f), new Vector4(0.50f, 0.20f, 0.10f, 1f), new Vector4(0.05f, 0.10f, 0.15f, 1f),
                                            new Vector4(0.60f, 0.30f, 0.0f, 1f), new Vector3(0.5f, 0.6f, 0.3f));
            var fogOn = new Vector4(1f, 0f, 1f, 0f);                // _WmvWLTerrain: lit, hemisphere, the fog
            var fogOff = new Vector4(1f, 0f, 0f, 0f);
            var modelsFog = new Vector4(0f, 0f, 0f, 1f);           // _WmvWLModels: no flat arm, no view, the fog
            WorldFogRecord rec = FogBaseRecord();
            WorldFogRecord beyond = FogRecordOf(new float[56]);    // F4 / F10: a start beyond the window -- visibility 1 everywhere
            rec.CopyTo(beyond);
            beyond.R[0] = 5000f;
            beyond.R[1] = 30000f;

            // ---- the terrain probe (step 5's: one white layer, no MCCV), its light normal tilted and not of unit length
            var terrain = new Material(textured) { name = "WmvWorldFog terrain probe" };
            made.Add(terrain);
            terrain.SetTexture("_WmvLayer0", Texture2D.whiteTexture);
            terrain.SetFloat("_WmvLayerCount", 1f);
            terrain.SetVector("_WmvRepeats", new Vector4(8f, 8f, 8f, 8f));
            terrain.SetFloat("_WmvTexturedView", 0f);
            terrain.SetFloat("_WmvVertexColors", 0f);
            terrain.SetFloat("_WmvLinearOut", 0f);
            terrain.SetFloat("_Cull", (float)CullMode.Off);
            var tn = new Vector3(0.2f, 0.9f, -0.1f);
            Mesh tq = FogProbeQuad(tn, made);
            var shaded = new double[3];
            TerrainOutdoorLight.Shade(new[] { 1.0, 1.0, 1.0 }, new double[] { tn.x, tn.y, tn.z }, new double[] { light.L.x, light.L.y, light.L.z },
                                      new double[] { light.Ambient.x, light.Ambient.y, light.Ambient.z },
                                      new double[] { light.Horizon.x, light.Horizon.y, light.Horizon.z },
                                      new double[] { light.Ground.x, light.Ground.y, light.Ground.z },
                                      new double[] { light.Direct.x, light.Direct.y, light.Direct.z }, false, shaded);
            double[] terrainLit = Rgba(shaded, 1.0);
            Func<int, double[]> terrainAt = i => terrainLit;

            // ---- F1: exponential, every regime over the window; the coverage counted on the twin
            float[] f1 = FogProbe(terrain, tq, true, light, fogOn, Vector4.zero, rec);
            int fogged1, atOne1;
            bool finite1;
            double e1 = FogProbeWorst(f1, terrainAt, rec, WorldFog.KindColour, false, out fogged1, out atOne1, out finite1);
            int hfMid = 0, pulled = 0, tinted = 0, ramp = 0, faded = 0;
            WorldFogRecord untinted = FogRecordWith(rec, 28, 0f);
            var a = new double[4];
            var b = new double[4];
            for (int j = 0; j < FogPixels; j++)
                for (int i = 0; i < FogPixels; i++)
                {
                    double[] p = FogPixelP(i, j);
                    double vis, hf;
                    WorldFog.Visibility(rec, p, FogUp, out vis, out hf);
                    if (hf > 0.0 && hf < 1.0) hfMid++;
                    if (Math.Abs(p[1]) > rec.R[35]) pulled++;
                    double raw = Math.Sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
                    if (raw > rec.R[55] && raw < rec.R[55] + rec.R[27]) ramp++;
                    if (raw > 0.3 * rec.R[1]) faded++;
                    WorldFog.Apply(rec, p, FogUp, terrainLit, WorldFog.KindColour, a);
                    WorldFog.Apply(untinted, p, FogUp, terrainLit, WorldFog.KindColour, b);
                    if (Math.Abs(a[0] - b[0]) > 1e-3) tinted++;
                }
            Check(finite1 && e1 <= FogGate && fogged1 > 0 && atOne1 > 0 && hfMid > 0 && pulled > 0 && tinted > 0 && ramp > 0 && faded > 0,
                  "outdoor fog F1: the lit terrain fogged (Wmv/TerrainTextured's world branch, WmvWorldFog) against TerrainOutdoorLight.Shade then " +
                  "WorldFog.Apply, worst channel " + FE(e1) + " (gate 5e-4, float32) over 256 pixels at 460-2,550 yd -- " + fogged1 + " fogged, " +
                  atOne1 + " before the start (visibility 1), " + faded + " in the end fade, " + ramp + " on the colour ramp (c^3 and c), " + hfMid +
                  " with hf strictly between 0 and 1, " + pulled + " beyond the pull (m 40), " + tinted + " tinted towards the sun (a 0.2, strength 0.3)",
                  log);
            WorldFogRecord noTint = FogRecordWith(rec, 20, 1f);     // R5.x = 1: SunFogColor zero
            float[] f1b = FogProbe(terrain, tq, true, light, fogOn, Vector4.zero, noTint);
            int foggedB, atOneB;
            bool finiteB;
            double e1b = FogProbeWorst(f1b, terrainAt, untinted, WorldFog.KindColour, false, out foggedB, out atOneB, out finiteB);
            Check(finiteB && e1b <= FogGate && foggedB > 0,
                  "outdoor fog F1: a record with R5.x = 1 (no tint: SunFogColor zero, most Retail keys) -- every pixel finite and the untinted " +
                  "twin's (strength 0), worst " + FE(e1b), log);

            // ---- F2: the power path (R1.x 1): raw distance, no height fog
            WorldFogRecord power = FogRecordOf(new float[56]);
            rec.CopyTo(power);
            power.R[4] = 1f;
            power.R[5] = 1800f;
            power.R[6] = (float)(1.0 / 1200.0);
            power.R[7] = 2.5f;
            int fogged2, atOne2;
            bool finite2;
            double e2 = FogProbeWorst(FogProbe(terrain, tq, true, light, fogOn, Vector4.zero, power), terrainAt, power, WorldFog.KindColour, false,
                                      out fogged2, out atOne2, out finite2);
            Check(finite2 && e2 <= FogGate && fogged2 > 0 && atOne2 > 0,
                  "outdoor fog F2: the power path (R1 = (1, 1800, 1/1200, 2.5): visibility 1 before 600 yd, 0 beyond 1,800) against the twin, " +
                  "worst " + FE(e2) + " (" + fogged2 + " fogged, " + atOne2 + " at 1)", log);

            // ---- F3: the polynomial path mixed in by R12.z 0.35, the cubics of the client's own fixture (R1's "art5" case)
            WorldFogRecord poly = FogRecordOf(new float[56]);
            rec.CopyTo(poly);
            float[] cubics = { 0.0906337127f, 1.4770968f, -0.125172406f, 0.0442426801f, 1.4033097f, 0.689362705f, 0.0484980755f, 0.0168574229f };
            for (int k = 0; k < 8; k++)
                poly.R[40 + k] = cubics[k];
            poly.R[48] = 2951.00342f;
            poly.R[49] = 380.735504f;
            poly.R[50] = 0.35f;
            int fogged3, atOne3;
            bool finite3;
            double e3 = FogProbeWorst(FogProbe(terrain, tq, true, light, fogOn, Vector4.zero, poly), terrainAt, poly, WorldFog.KindColour, false,
                                      out fogged3, out atOne3, out finite3);
            Check(finite3 && e3 <= FogGate && fogged3 > 0,
                  "outdoor fog F3: the polynomial path (over 380.7-2,951 yd, mixed in by 0.35) against the twin, worst " + FE(e3) + " (" + fogged3 +
                  " fogged)", log);

            // ---- F4: visibility 1 is exact -- a start beyond the window draws the fog-off render bit for bit; F5: slot 0
            float[] f4on = FogProbe(terrain, tq, true, light, fogOn, Vector4.zero, beyond);
            float[] f4off = FogProbe(terrain, tq, true, light, fogOff, Vector4.zero, beyond);
            float[] f5 = FogProbe(terrain, tq, true, light, fogOff, Vector4.zero, rec);
            WorldFogRecord beyondInf = FogRecordWith(beyond, 12, float.PositiveInfinity);   // F4b / F10b: R3.xyz +infinity
            beyondInf.R[13] = beyondInf.R[14] = double.PositiveInfinity;
            float[] f4inf = FogProbe(terrain, tq, true, light, fogOn, Vector4.zero, beyondInf);
            double e4 = FogProbeWorst(f4off, terrainAt, null, 0, false), e5 = FogProbeWorst(f5, terrainAt, null, 0, false);
            int fogged4, atOne4;
            bool finite4;
            FogProbeWorst(f4on, terrainAt, beyond, WorldFog.KindColour, false, out fogged4, out atOne4, out finite4);
            Check(FogSame(f4on, f4off) == 256 && atOne4 == 256 && e4 <= 2e-3,
                  "outdoor fog F4: a record whose start (5,000 yd) lies beyond the window -- the fog-on terrain equals its _WmvWLTerrain.z 0 render " +
                  "bit for bit, " + FogSame(f4on, f4off) + " of 256 pixels (visibility exactly 1 at " + atOne4 + "), and that is step 5's twin within " +
                  FE(e4) + " (2e-3)", log);
            Check(FogSame(f4inf, f4off) == 256,
                  "outdoor fog F4b: F4's record with the fog colour R3.xyz +infinity (the fog target not finite) still draws the _WmvWLTerrain.z 0 " +
                  "render bit for bit, " + FogSame(f4inf, f4off) + " of 256 pixels -- the output select returns the input at visibility 1, where the " +
                  "mix alone, mad(c, vis, target x (1 - vis)), would give NaN (a target not finite times 0)", log);
            Check(FogSame(f5, f4off) == 256 && e5 <= FogGate,
                  "outdoor fog F5: the terrain's fog slot 0 (_WmvWLTerrain.z 0) draws no fog whatever the record holds -- F1's record gives the " +
                  "same render as F4's, " + FogSame(f5, f4off) + " of 256 pixels, the lit twin within " + FE(e5), log);

            // ---- F14: the composite's pin -- without the probe's own _WmvWLOn the terrain is the unlit composite, whatever the
            // light's and the fog's globals hold (published "on, lit, fogged" as WmvWorldLight leaves them)
            Shader.SetGlobalFloat(WmvWorldLight.OnId, 1f);
            Shader.SetGlobalVector(WmvWorldLight.TerrainId, fogOn);
            float[] f14 = FogProbe(terrain, tq, false, light, fogOn, Vector4.zero, rec);
            int white = 0;
            for (int i = 0; i < FogPixels * FogPixels; i++)
                if (f14[4 * i] == 1f && f14[4 * i + 1] == 1f && f14[4 * i + 2] == 1f)
                    white++;
            Check(white == 256 && Shader.GetGlobalFloat(WmvWorldLight.OnId) == 1f,
                  "outdoor fog F14: inside WmvTerrainComposite's render without the probe's own _WmvWLOn the terrain draws the unlit composite " +
                  "(" + white + " of 256 pixels exactly white) although the light's and the fog's globals say on, lit and fogged; _WmvWLOn put back " +
                  "to 1 after", log);

            // ---- the M2 and world-model probes: copies through WmvWorldLitMaterials.Copy of step 6's minimal originals
            var texel0 = new Color32(153, 115, 77, 255);
            var texel1 = new Color32(90, 160, 200, 255);
            Texture2D tex = ModelProbeTexture(texel0, texel1, made);
            double[][] albedo = { ModelProbeAlbedo(texel0), ModelProbeAlbedo(texel1) };
            Func<int, double[]> albedoAt = i => albedo[i < FogPixels / 2 ? 0 : 1];
            var mn = new Vector3(0.2f, 0.9f, -0.1f);
            Mesh mq = FogProbeQuad(mn, made);
            double[] normal = ModelProbeNormal(mn, mn, 0);
            Func<int, double[]> m2Lit = i => Rgba(light.Twin(false, albedoAt(i), normal, false), 1.0);
            Func<int, double[]> wmoLit = i => Rgba(light.Twin(true, albedoAt(i), normal, false), 1.0);
            Func<int, double[]> baseAt = i => Rgba(albedoAt(i), 1.0);
            Material m2 = ModelProbeM2(m2Shader, tex, "WmvWorldFog probe (M2)", made);
            Material wmo = ModelProbeWmo(wmoShader, tex, "WmvWorldFog probe (WMO)", made);
            Material m2Copy = ModelProbeCopy(m2, m2Variant, made), wmoCopy = ModelProbeCopy(wmo, wmoVariant, made);

            // ---- F6: M2 opaque (kind 0), and alpha-keyed (_ALPHATEST_ON, texel 0 of alpha 0)
            double e6 = FogProbeWorst(FogProbe(m2Copy, mq, true, light, fogOn, modelsFog, rec), m2Lit, rec, WorldFog.KindColour, true);
            Texture2D keyedTex = ModelProbeTexture(new Color32(texel0.r, texel0.g, texel0.b, 0), texel1, made);
            Material m2Key = ModelProbeM2(m2Shader, keyedTex, "WmvWorldFog probe (M2, alpha key)", made);
            string described;
            WmvModelBuilder.ApplyBlendMode(m2Key, M2BlendMode.AlphaKey, false, 0.5f, out described);
            Material m2KeyCopy = ModelProbeCopy(m2Key, m2Variant, made);
            float[] f6k = FogProbe(m2KeyCopy, mq, true, light, fogOn, modelsFog, rec);
            bool cleared = true;
            for (int j = 0; j < FogPixels; j++)
                for (int i = 0; i < FogPixels / 2; i++)
                    for (int k = 0; k < 4; k++)
                        cleared &= f6k[4 * (j * FogPixels + i) + k] == 0f;
            int fogged6, atOne6;
            bool finite6;
            double e6k = FogProbeWorst(f6k, i => i < FogPixels / 2 ? null : m2Lit(i), rec, WorldFog.KindColour, true, out fogged6, out atOne6, out finite6);
            Check(e6 <= FogGate && finite6 && e6k <= FogGate && cleared && fogged6 > 0 && m2KeyCopy.IsKeywordEnabled("_ALPHATEST_ON"),
                  "outdoor fog F6: the M2 variant fogged towards the fog colour (kind 0, alpha unchanged) -- opaque " + FE(e6) + "; alpha-keyed " +
                  "(_ALPHATEST_ON): the texel of alpha 0 keeps the clear value " + (cleared ? "exactly" : "NOT") + " (the alpha test before the fog, " +
                  "as the client's), the other fogged, " + FE(e6k), log);

            // ---- F7: the fog targets 1-4, set directly in _WmvFogBatch.y while the probe draws One / Zero
            var kindWorst = new double[WorldFog.Kinds];
            for (int kind = WorldFog.KindBlack; kind <= WorldFog.KindFade; kind++)
            {
                m2Copy.SetVector(WmvWorldLitMaterials.FogBatchId, new Vector4(0f, kind, 0f, 0f));
                kindWorst[kind] = FogProbeWorst(FogProbe(m2Copy, mq, true, light, fogOn, modelsFog, rec), m2Lit, rec, kind, true);
            }
            m2Copy.SetVector(WmvWorldLitMaterials.FogBatchId, Vector4.zero);
            Check(kindWorst[1] <= FogGate && kindWorst[2] <= FogGate && kindWorst[3] <= FogGate && kindWorst[4] <= FogGate,
                  "outdoor fog F7: the M2 variant's fog targets against the twin, rgba -- 1 rgb x vis (black) " + FE(kindWorst[1]) + ", 2 towards " +
                  "white " + FE(kindWorst[2]) + ", 3 towards grey 0.5 " + FE(kindWorst[3]) + ", 4 rgba x vis (the client's blend word 4; alpha " +
                  "checked) " + FE(kindWorst[4]), log);

            // ---- F8: NoFog (_WmvFogBatch.x 1) is the fog-off render exactly; unlit (_Emissive 1) is fogged; the albedo view is not
            var modelsOff = Vector4.zero;
            float[] slot0 = FogProbe(m2Copy, mq, true, light, fogOn, modelsOff, rec);
            m2Copy.SetVector(WmvWorldLitMaterials.FogBatchId, new Vector4(1f, 0f, 0f, 0f));
            float[] noFog = FogProbe(m2Copy, mq, true, light, fogOn, modelsFog, rec);
            m2Copy.SetVector(WmvWorldLitMaterials.FogBatchId, Vector4.zero);
            Material m2Unlit = ModelProbeM2(m2Shader, tex, "WmvWorldFog probe (M2, unlit)", made);
            m2Unlit.SetFloat("_Emissive", 1f);
            Material m2UnlitCopy = ModelProbeCopy(m2Unlit, m2Variant, made);
            double e8u = FogProbeWorst(FogProbe(m2UnlitCopy, mq, true, light, fogOn, modelsFog, rec), baseAt, rec, WorldFog.KindColour, true);
            float[] albedoView = FogProbe(m2Copy, mq, true, light, fogOn, new Vector4(0f, 0f, 1f, 1f), rec);
            float[] albedoViewOff = FogProbe(m2Copy, mq, true, light, fogOn, new Vector4(0f, 0f, 1f, 0f), rec);
            double e8a = FogProbeWorst(albedoView, baseAt, null, 0, true);
            Check(FogSame(noFog, slot0) == 256 && e8u <= FogGate && FogSame(albedoView, albedoViewOff) == 256 && e8a <= FogGate,
                  "outdoor fog F8: an M2 batch flagged NoFog (_WmvFogBatch.x 1) draws the fog-off render exactly (" + FogSame(noFog, slot0) + " of 256); " +
                  "an unlit one (_Emissive 1) is fogged from its base (" + FE(e8u) + "); the albedo view (_WmvWLModels.z 1) is not fogged -- " +
                  FogSame(albedoView, albedoViewOff) + " of 256 equal to the view with the fog slot 0, the base within " + FE(e8a), log);

            // ---- F9: the world model -- lit, F_UNLIT (_WmoLightBypass 1) fogged, F_UNFOGGED skipped
            double e9 = FogProbeWorst(FogProbe(wmoCopy, mq, true, light, fogOn, modelsFog, rec), wmoLit, rec, WorldFog.KindColour, true);
            Material wmoUnlit = ModelProbeWmo(wmoShader, tex, "WmvWorldFog probe (WMO, F_UNLIT)", made);
            wmoUnlit.SetFloat("_WmoLightBypass", 1f);
            Material wmoUnlitCopy = ModelProbeCopy(wmoUnlit, wmoVariant, made);
            double e9u = FogProbeWorst(FogProbe(wmoUnlitCopy, mq, true, light, fogOn, modelsFog, rec), baseAt, rec, WorldFog.KindColour, true);
            float[] wmoSlot0 = FogProbe(wmoCopy, mq, true, light, fogOn, modelsOff, rec);
            wmoCopy.SetVector(WmvWorldLitMaterials.FogBatchId, new Vector4(1f, 0f, 0f, 0f));
            float[] wmoUnfogged = FogProbe(wmoCopy, mq, true, light, fogOn, modelsFog, rec);
            wmoCopy.SetVector(WmvWorldLitMaterials.FogBatchId, Vector4.zero);
            Check(e9 <= FogGate && e9u <= FogGate && FogSame(wmoUnfogged, wmoSlot0) == 256,
                  "outdoor fog F9: the world-model variant fogged (the M2 tail exactly) -- lit " + FE(e9) + ", F_UNLIT (_WmoLightBypass 1) from its " +
                  "albedo " + FE(e9u) + "; F_UNFOGGED (_WmvFogBatch.x 1) draws the fog-off render exactly, " + FogSame(wmoUnfogged, wmoSlot0) +
                  " of 256", log);

            // ---- F10: the fog slot 0 in the variants is step 6's -- the twin within 2e-3, and bit for bit a fog-on render whose
            // record puts every pixel before the start
            double e10m = FogProbeWorst(slot0, m2Lit, null, 0, true), e10w = FogProbeWorst(wmoSlot0, wmoLit, null, 0, true);
            float[] m2Before = FogProbe(m2Copy, mq, true, light, fogOn, modelsFog, beyond);
            float[] wmoBefore = FogProbe(wmoCopy, mq, true, light, fogOn, modelsFog, beyond);
            Check(e10m <= 2e-3 && e10w <= 2e-3 && FogSame(m2Before, slot0) == 256 && FogSame(wmoBefore, wmoSlot0) == 256,
                  "outdoor fog F10: the variants with the fog slot 0 (_WmvWLModels.w 0) draw step 6's light -- the twin (ModelOutdoorLight) within " +
                  FE(e10m) + " (M2) and " + FE(e10w) + " (world model), 2e-3 -- and equal a fog-on render whose start lies beyond the window, " +
                  FogSame(m2Before, slot0) + " and " + FogSame(wmoBefore, wmoSlot0) + " of 256 pixels", log);
            float[] m2BeforeInf = FogProbe(m2Copy, mq, true, light, fogOn, modelsFog, beyondInf);
            float[] wmoBeforeInf = FogProbe(wmoCopy, mq, true, light, fogOn, modelsFog, beyondInf);
            Check(FogSame(m2BeforeInf, slot0) == 256 && FogSame(wmoBeforeInf, wmoSlot0) == 256,
                  "outdoor fog F10b: F4b's record (start beyond the window, fog colour +infinity) on the variants still equals the fog slot 0 render " +
                  "bit for bit -- " + FogSame(m2BeforeInf, slot0) + " (M2) and " + FogSame(wmoBeforeInf, wmoSlot0) + " (world model) of 256 pixels: " +
                  "the output select, not the mix", log);

            // ---- F11: the originals read no fog global: every one set and _WmvWLOn 1, or _WmvWLOn 0 and none set
            float[] o1 = FogProbe(m2, mq, true, light, fogOn, modelsFog, rec), o0 = FogProbe(m2, mq, false, light, Vector4.zero, Vector4.zero, null);
            float[] w1 = FogProbe(wmo, mq, true, light, fogOn, modelsFog, rec), w0 = FogProbe(wmo, mq, false, light, Vector4.zero, Vector4.zero, null);
            Check(FogSame(o1, o0) == 256 && FogSame(w1, w0) == 256 && ModelProbeDiff(o1, slot0) > 1e-3,
                  "outdoor fog F11: the originals (WmvOpaque, WmvWmo) draw exactly the same with every fog global set, the fog slots 1 and the " +
                  "probe's _WmvWLOn 1 as with _WmvWLOn 0 -- " + FogSame(o1, o0) + " and " + FogSame(w1, w0) + " of 256 pixels", log);
        }
        finally
        {
            foreach (UnityEngine.Object o in made)
                if (o != null)
                    UnityEngine.Object.DestroyImmediate(o);
        }
    }

    // ---------------------------------------------------------------- F12: the per-copy fog vectors through the real managers

    static string FogBatchText(Vector4 v)
    {
        return "(" + v.x.ToString("0.##", System.Globalization.CultureInfo.InvariantCulture) + ", " +
               v.y.ToString("0.##", System.Globalization.CultureInfo.InvariantCulture) + ")";
    }

    static void OutdoorFogBindingTests(Action<string> log)
    {
        int meshes0 = Meshes(), materials0 = Materials(), models0 = WmvRuntimeModel.Live, set0 = WmvWorldLitMaterials.FogBatchesSet;
        string doodads0 = DoodadTallies(), world0 = WorldTallies();
        int wc0 = WmvWorldModels.LiveCopies, dc0 = WmvWorldDoodads.LiveCopies;
        var holder = new GameObject("_WmvWorldFogBindingTest");
        holder.SetActive(false);
        var doodads = new WmvWorldDoodads(holder.transform);
        var cache = new WmvDoodadTextures();
        var view = new TestWorldView();
        var owned = new List<UnityEngine.Object>();
        try
        {
            // ---- a doodad: the nine M2 blend modes, batch 1 flagged NoFog (0x02)
            int[] flags = { 0, 0x02, 0, 0, 0, 0, 0, 0, 0 };
            int[] blends = { 0, 0, 1, 2, 3, 4, 5, 6, 7 };
            M2ParsedModel model = M2Parser.Parse(M2Synthetic.MaterialModeModel(flags, blends));
            M2ParsedSkin skin = M2SkinParser.Parse(M2Synthetic.MaterialModeSkin(flags.Length));
            WmvDoodadPrototype dp = doodads.BuildPrototype(9711, model, skin, new Dictionary<int, BlpImage>(), new Dictionary<int, uint>(), cache);
            if (dp.Runtime != null)
            {
                owned.Add(dp.Runtime.Root);
                owned.Add(dp.Runtime.Mesh);
                owned.AddRange(dp.Runtime.Materials);
            }
            var doodad = new WorldDoodad
            {
                Source = DoodadSource.Mddf, FileDataID = 9711, UniqueId = 96, Position = new WorldPoint(0.0, 0.0, 0.0),
                Rotation = new double[] { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, Scale = 1.0, Context = WorldLightContext.Outdoor,
            };
            WmvDoodadInstance di = doodads.Place(dp, doodad, new WorldOrigin(0.0, 0.0, 0.0), 1);
            owned.Add(di.Root);
            var bd = new WmvWorldLitBind { Lit = true };
            doodads.RebindWorldLit(true, bd);
            owned.AddRange(dp.WorldLitCopies);
            Material[] bound = di.Renderer.sharedMaterials;
            float[] wantX = { 0, 1, 0, 0, 0, 0, 0, 0, 0 };
            float[] wantY = { 0, 0, 0, 0, 1, 1, 2, 3, 4 };
            var got = new List<string>();
            bool vectors = bound.Length == flags.Length && dp.Runtime != null && dp.Runtime.Bindings.Length == flags.Length && dp.Runtime.Bindings[1].NoFog &&
                           !dp.Runtime.Bindings[0].NoFog;
            for (int k = 0; k < bound.Length; k++)
            {
                Material c = bound[k];
                bool copy = IsWorldLit(c) && c.HasProperty(WmvWorldLitMaterials.FogBatchId);
                Vector4 f = copy ? c.GetVector(WmvWorldLitMaterials.FogBatchId) : new Vector4(-1f, -1f, 0f, 0f);
                got.Add(FogBatchText(f));
                vectors &= copy && k < wantX.Length && f.x == wantX[k] && f.y == wantY[k] && f.z == 0f && f.w == 0f;
            }
            string dLine = bd.Describe(null);
            bool dCounts = bd.Doodads == 1 && bd.FogUnfogged == 1 && bd.FogKinds[WorldFog.KindColour] == 3 && bd.FogKinds[WorldFog.KindBlack] == 2 &&
                           bd.FogKinds[WorldFog.KindWhite] == 1 && bd.FogKinds[WorldFog.KindGrey] == 1 && bd.FogKinds[WorldFog.KindFade] == 1 &&
                           bd.FogUnknownBlend == 0 && bd.MomtBlend2 == 0 &&
                           dLine.Contains("; fog: 1 unfogged (NoFog / F_UNFOGGED), targets: colour 3, black 2, white 1, grey 1, fade 1, unknown blend 0; " +
                                          "world-model MOMT blend 2+: 0; ");
            Check(vectors && dCounts,
                  "outdoor fog F12: a doodad of the nine M2 blend modes, batch 1 flagged NoFog, built and bound by the real WmvWorldDoodads -- its " +
                  "copies' _WmvFogBatch (x, y) " + string.Join(" ", got.ToArray()) + " (opaque, NoFog, alpha key, alpha: the fog colour; additive " +
                  "x2: black; modulate: white; modulate 2x: grey; mode 7: the fade); the binding line counts them before its time (\"" +
                  (dLine.IndexOf("; fog:", StringComparison.Ordinal) >= 0 ? dLine.Substring(dLine.IndexOf("; fog:", StringComparison.Ordinal)) : dLine) + "\")",
                  log);

            // ---- a world model: three exterior groups drawing MOMT entries 0 (plain), 1 (F_UNFOGGED) and 2 (MOMT blend 2)
            const uint wid = 9720;
            WmoRoot root = WmoParser.ParseRoot(WmoSynthetic.ContextRoot(new[] { WmoGroupFlags.Outdoor, WmoGroupFlags.Outdoor, WmoGroupFlags.Outdoor }, wid + 1,
                                                                        wid + 50, null, new[] { 0u, WmoMaterialSemantics.FlagUnfogged, 0u },
                                                                        new uint[] { 0, 0, 2 }), "fog root " + wid);
            var groups = new[]
            {
                WmoParser.ParseGroup(WmoSynthetic.ContextGroup(0, WmoGroupFlags.Outdoor, 0), "fog group 0", 0),
                WmoParser.ParseGroup(WmoSynthetic.ContextGroup(1, WmoGroupFlags.Outdoor, 1), "fog group 1", 1),
                WmoParser.ParseGroup(WmoSynthetic.ContextGroup(2, WmoGroupFlags.Outdoor, 2), "fog group 2", 2),
            };
            WmvWorldPrototype wp = view.BuildPrototype(wid, root, groups, new Dictionary<uint, WmvWmoTexture>());
            WmvWorldInstance wi = view.PlaceInstance(wp, ContextPlacement(wid, 97), new WorldOrigin(0.0, 0.0, 0.0), 1);
            var bm = new WmvWorldLitBind { Lit = true };
            view.Models.RebindWorldLit(true, bm);
            var groupVector = new Vector4[3];
            var groupCopy = new bool[3];
            Renderer[] all = wp.Runtime.Root.GetComponentsInChildren<Renderer>(true);
            for (int k = 0; k < all.Length && k < wi.Renderers.Length; k++)
            {
                int gi = Array.IndexOf(wp.Runtime.GroupObjects, all[k].gameObject);
                Material c = wi.Renderers[k] != null ? wi.Renderers[k].sharedMaterial : null;
                if (gi < 0 || gi > 2 || c == null)
                    continue;
                groupCopy[gi] = IsWorldLit(c);
                groupVector[gi] = c.HasProperty(WmvWorldLitMaterials.FogBatchId) ? c.GetVector(WmvWorldLitMaterials.FogBatchId) : new Vector4(-1f, -1f, 0f, 0f);
            }
            bool momt = wp.Runtime.MaterialInfo.Length == 3 && (wp.Runtime.MaterialInfo[1].Flags & WmoMaterialSemantics.FlagUnfogged) != 0 &&
                        wp.Runtime.MaterialInfo[2].Blend == 2;
            bool wVectors = groupCopy[0] && groupCopy[1] && groupCopy[2] && groupVector[0] == Vector4.zero && groupVector[1] == new Vector4(1f, 0f, 0f, 0f) &&
                            groupVector[2] == Vector4.zero;
            string wLine = bm.Describe(null);
            bool wCounts = bm.GroupRenderers == 3 && bm.FogUnfogged == 1 && bm.FogKinds[WorldFog.KindColour] == 2 && bm.MomtBlend2 == 1 &&
                           bm.FogUnknownBlend == 0 &&
                           wLine.Contains("; fog: 1 unfogged (NoFog / F_UNFOGGED), targets: colour 2, black 0, white 0, grey 0, fade 0, unknown blend 0; " +
                                          "world-model MOMT blend 2+: 1; ");
            Check(momt && wVectors && wCounts,
                  "outdoor fog F12: a world model of three exterior groups (WmoSynthetic.ContextRoot with material flags and blends) bound by the " +
                  "real WmvWorldModels -- the plain entry's copy (0, 0), the F_UNFOGGED one's " + FogBatchText(groupVector[1]) + ", the MOMT blend 2 " +
                  "one's " + FogBatchText(groupVector[2]) + " (drawn One / Zero provisionally: the fog colour); the binding line counts one unfogged " +
                  "batch and one MOMT blend 2+ (\"" + (wLine.IndexOf("; fog:", StringComparison.Ordinal) >= 0
                      ? wLine.Substring(wLine.IndexOf("; fog:", StringComparison.Ordinal)) : wLine) + "\")", log);

            // ---- a rebind: the counts are per slot (the codes kept), no copy is flagged again
            int setBefore = WmvWorldLitMaterials.FogBatchesSet;
            doodads.RebindWorldLit(false, null);
            view.Models.RebindWorldLit(false, null);
            var bd2 = new WmvWorldLitBind { Lit = true };
            var bm2 = new WmvWorldLitBind { Lit = true };
            doodads.RebindWorldLit(true, bd2);
            view.Models.RebindWorldLit(true, bm2);
            bool same = bd2.FogUnfogged == bd.FogUnfogged && bd2.FogUnknownBlend == bd.FogUnknownBlend && bm2.FogUnfogged == bm.FogUnfogged &&
                        bm2.MomtBlend2 == bm.MomtBlend2 && WmvWorldLitMaterials.FogBatchesSet == setBefore && setBefore - set0 == 12;
            for (int k = 0; k < WorldFog.Kinds; k++)
                same &= bd2.FogKinds[k] == bd.FogKinds[k] && bm2.FogKinds[k] == bm.FogKinds[k];
            doodads.RebindWorldLit(false, null);
            view.Models.RebindWorldLit(false, null);
            owned.AddRange(cache.All);
            doodads.DisposeInstance(di);
            doodads.DisposePrototype(dp, cache);
            cache.Dispose();
            view.DisposeInstance(wi);
            view.DisposePrototype(wp);
            foreach (UnityEngine.Object o in owned)
                if (o != null)
                    UnityEngine.Object.DestroyImmediate(o);
            owned.Clear();
            bool clean = DoodadTallies() == doodads0 && WorldTallies() == world0 && Meshes() == meshes0 && Materials() == materials0 &&
                         WmvRuntimeModel.Live == models0 && WmvWorldModels.LiveCopies == wc0 && WmvWorldDoodads.LiveCopies == dc0;
            Check(same && clean,
                  "outdoor fog F12: after a rebind to the originals and back the binding lines count the same fog (the codes are kept per slot; " +
                  (setBefore - set0) + " fog vectors written for 12 copies, none again); every tally back to its start after disposal (doodads " +
                  DoodadTallies() + ", world " + WorldTallies() + ", materials " + (Materials() - materials0) + " more than at the start)", log);
        }
        finally
        {
            foreach (UnityEngine.Object o in owned)
                if (o != null)
                    UnityEngine.Object.DestroyImmediate(o);
            view.Destroy();
            UnityEngine.Object.DestroyImmediate(holder);
        }
    }

    // ---------------------------------------------------------------- F13, F15: the driver

    /// <summary>The light tests' loadWorld line (WorldLightLine) with every column the fog record is made of sent, as the host
    /// sends them: the five fog colours and the 25 fog floats besides FogDensity (whose keys stay as they were), the same values
    /// on each of the three keys -- FogScaler 0.2 (a start of 600 yd), the plane at game Z -40 with rate 0.02 and dawn's height
    /// cubic, a ramp of 1,500 yd, a sun tint, no polynomial path. The light tests' own object sends three colours and FogDensity
    /// only, so it draws no fog (WorldFog.AbsentColumns). `product` replaces "wow".</summary>
    static string FogWorldLightLine(string product)
    {
        const string colours = ",\"SkyFogColor\",\"EndFogColor\",\"SunFogColor\",\"FogHeightColor\",\"EndFogHeightColor\"";
        const string floats = ",\"FogEnd\",\"FogScaler\",\"FogHeight\",\"FogHeightScaler\",\"FogHeightDensity\",\"FogZScalar\",\"MainFogStartDist\"," +
                              "\"MainFogEndDist\",\"SunFogAngle\",\"EndFogColorDistance\",\"FogStartOffset\",\"SunFogStrength\",\"FogHeightCoefficients0\"," +
                              "\"FogHeightCoefficients1\",\"FogHeightCoefficients2\",\"FogHeightCoefficients3\",\"MainFogCoefficients0\"," +
                              "\"MainFogCoefficients1\",\"MainFogCoefficients2\",\"MainFogCoefficients3\",\"HeightDensityFogCoeff0\"," +
                              "\"HeightDensityFogCoeff1\",\"HeightDensityFogCoeff2\",\"HeightDensityFogCoeff3\"";
        const string c = ",8427696,6978704,13672560,7371912,0";       // 0x8098B0, 0x6A7C90, 0xD0A070, 0x707C88, 0 (EndFog's fall-back)
        const string f = ",1200,0.2,-40,0.02,1.5,40,0,0,0.3,1500,0,0.25,0.00078,0.43267,0.39225,0.18041,0,0,0,0,0,0,0,0";
        return WorldLightLine("")
            .Replace("\"colorColumns\":[\"DirectColor\",\"AmbientColor\",\"SkyMiddleColor\"]",
                     "\"colorColumns\":[\"DirectColor\",\"AmbientColor\",\"SkyMiddleColor\"" + colours + "]")
            .Replace("\"floatColumns\":[\"FogDensity\"]", "\"floatColumns\":[\"FogDensity\"" + floats + "]")
            .Replace("\"c\":[7885824,10001307,15576168,6173184,12035512,16769711],\"f\":[2.5,3.0]",
                     "\"c\":[7885824,10001307,15576168" + c + ",6173184,12035512,16769711" + c + "],\"f\":[2.5" + f + ",3.0" + f + "]")
            .Replace("\"c\":[16777215,16777215,16777215],\"f\":[9.0]", "\"c\":[16777215,16777215,16777215" + c + "],\"f\":[9.0" + f + "]")
            .Replace("\"product\":\"wow\"", "\"product\":\"" + product + "\"");
    }

    static void OutdoorFogDriverTests(Action<string> log)
    {
        Action<string> savedLog = WmvWorldLight.Log;
        Vector3 savedEye = WmvWorldLight.Eye;
        var said = new List<string>();
        try
        {
            WmvWorldLight.Log = said.Add;
            WorldLightSwitches sw = WmvWorldLight.Switches;
            WorldLightSet s = WmvIpcClient.ParseWorldLight(FogWorldLightLine("wow"));
            WorldLightSet era = WmvIpcClient.ParseWorldLight(FogWorldLightLine("wow_classic_era"));
            bool sent = WorldFog.AbsentColumns(s, WorldFogMode.RetailPattern) == null && WorldFog.AbsentColumns(era, WorldFogMode.ClassicPublic) == null &&
                        s.DroppedDays == 0 && s.DayCount == 2;
            WmvWorldLight.ApplySwitches();
            WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasOn = true, On = true, HasClock = true, Clock = "pinned", HasTime = true,
                                                                           Time = "12:00:00" });
            const int F = -6000;                                   // frame numbers Time.frameCount never takes
            double x = 1225, y = -3948, zz = 20, origin = 17.5;    // inside zone 139, 75 yd from the point light: weight 0.5
            var e1 = new Vector3(5f, 37.5f, -8f);
            var e2 = new Vector3(5f, 80.25f, -8f);
            WmvWorldLight.SetEye(e1);
            WmvWorldLight.Frame(F, 0.016, s, true, x, y, zz, origin, true);
            // The pure model: the evaluator at the camera, Pack with the camera's game height, the sun as the sky gets it.
            var ev = new WorldLightEvaluator();
            var smp = new WorldLightSample();
            ev.Evaluate(s, x, y, zz, 12 * 3600.0, 0, sw.Rules, smp);
            double sx, sy, sz;
            WorldLightModel.SunDirection(12 * 3600.0, out sx, out sy, out sz);
            Vector4 sun = WmvWorldLight.Direction(new[] { sx, sy, sz });
            WorldFogMode mode = WorldFog.ModeFor(s.Product, sw.Fog, sw.FogClassic);
            var want = new WorldFogRecord();
            WorldFog.Pack(smp.V, WmvWorldLight.FarClip, WmvWorldLight.HorizonStart, zz, WmvWorldLight.Clock.HalfMinute, new double[] { sun.x, sun.y, sun.z },
                          s.MapFlags, mode, want);
            bool fog = mode != WorldFogMode.Off;
            var wrongRows = new List<string>();
            Func<Vector3, bool> rowsEqual = eye =>
            {
                bool ok = true;
                for (int k = 0; k < WorldFogRecord.Rows; k++)
                {
                    Vector4 g = Shader.GetGlobalVector(WmvWorldLight.FogRowIds[k]);
                    double w = k == 2 ? WorldFog.PlaneOffset(eye.y + origin, want) : want.R[4 * k + 3];
                    if (g.x != (float)want.R[4 * k] || g.y != (float)want.R[4 * k + 1] || g.z != (float)want.R[4 * k + 2] || g.w != (float)w)
                    {
                        ok = false;
                        wrongRows.Add("R" + k + " " + g);
                    }
                }
                return ok && SameVector(Shader.GetGlobalVector(WmvWorldLight.EyeId), eye.x, eye.y, eye.z, 1.0);
            };
            bool first = !fog || rowsEqual(e1);
            float planeFirst = Shader.GetGlobalVector(WmvWorldLight.FogRowIds[2]).w;
            WmvWorldLight.SetEye(e2);
            WmvWorldLight.Frame(F, 0.016, s, true, x, y, zz, origin, true);        // the same frame again: published at the second eye
            bool second = !fog || rowsEqual(e2);
            float planeSecond = Shader.GetGlobalVector(WmvWorldLight.FogRowIds[2]).w;
            float slots = fog ? 1f : 0f;
            bool slotsOk = Shader.GetGlobalVector(WmvWorldLight.TerrainId).z == slots && Shader.GetGlobalVector(WmvWorldLight.ModelsId).w == slots &&
                           WmvWorldLight.FogDrawn == fog && WmvWorldLight.FogMode == mode;
            bool samePack = fog && want.Mode == mode && planeFirst != planeSecond;
            Check(sent && WmvWorldLight.PublishedOn && first && second && slotsOk && (samePack || !fog),
                  "outdoor fog F13: after a Frame at 12:00 on the test's object with every fog column sent (product \"wow\": " + WorldFog.ModeName(mode) +
                  "; " + (sent ? "none absent" : "SOME ABSENT") + ") the 14 published " +
                  "rows equal WorldFog.Pack of the pure model (WorldLightEvaluator at the camera, float equality after the cast), R2.w the plane " +
                  "offset of the eye SetEye gave -- " + planeFirst.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture) + " at eye y " +
                  e1.y + ", then " + planeSecond.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture) + " at eye y " + e2.y + " in " +
                  "the same frame (origin Z " + origin + "), _WmvWLEye that eye; _WmvWLTerrain.z and _WmvWLModels.w " + slots +
                  (wrongRows.Count > 0 ? " -- WRONG: " + string.Join(", ", wrongRows.ToArray()) : ""), log);

            // A product the fog is off for: no fog global is written (sentinels kept), both slots 0.
            WorldFogMode eraMode = WorldFog.ModeFor(era.Product, sw.Fog, sw.FogClassic);
            var sentinelRow = new Vector4(-7.25f, 3.5f, 9.75f, -1.5f);
            var sentinelEye = new Vector4(123.5f, -45.25f, 6.75f, 0.5f);
            Shader.SetGlobalVector(WmvWorldLight.FogRowIds[0], sentinelRow);
            Shader.SetGlobalVector(WmvWorldLight.EyeId, sentinelEye);
            WmvWorldLight.Frame(F + 1, 0.016, era, true, x, y, zz, origin, true);
            bool eraShown = WmvWorldLight.PublishedOn && WmvWorldLight.ObjectsLit;
            bool kept = Shader.GetGlobalVector(WmvWorldLight.FogRowIds[0]) == sentinelRow && Shader.GetGlobalVector(WmvWorldLight.EyeId) == sentinelEye;
            bool eraSlots = Shader.GetGlobalVector(WmvWorldLight.TerrainId).z == 0f && Shader.GetGlobalVector(WmvWorldLight.ModelsId).w == 0f &&
                            !WmvWorldLight.FogDrawn;
            string eraDescribe = WmvWorldLight.Describe();
            string eraClause = sw.Fog ? "; fog off for product wow_classic_era (D17/D18)" : "; fog OFF (-wmvFog=off)";
            Check(eraMode != WorldFogMode.Off || (eraShown && kept && eraSlots && eraDescribe.Contains(eraClause)),
                  "outdoor fog F13: the same object as Classic Era (\"wow_classic_era\", " + WorldFog.ModeName(eraMode) + ") lit but unfogged -- the " +
                  "values written into _WmvWLFogR0 and _WmvWLEye before its frame are unchanged after it, _WmvWLTerrain.z and _WmvWLModels.w 0; " +
                  "the capture line says so (\"" + (eraDescribe.IndexOf("; fog", StringComparison.Ordinal) >= 0
                      ? eraDescribe.Substring(eraDescribe.IndexOf("; fog", StringComparison.Ordinal)) : eraDescribe) + "\")", log);

            // The null set (T14's guard of the same handling): no throw, the light not failed off, nothing shown. The census is said
            // for each object that differs from the last one counted (the first object again after Era: a third line), and not again
            // for the same object after ApplySwitches (ForgetFrame) and the host's "on".
            WmvWorldLight.Frame(F + 2, 0.016, null, false, 0, 0, 0, 0, false);
            bool nullOk = WmvWorldLight.Enabled && !WmvWorldLight.PublishedOn && WmvWorldLight.FogRecord.Mode == WorldFogMode.Off;
            WmvWorldLight.Frame(F + 3, 0.016, s, true, x, y, zz, origin, true);
            string describe = WmvWorldLight.Describe();
            int censusBefore = said.FindAll(l => l.StartsWith("world light: fog for map ", StringComparison.Ordinal)).Count;
            WmvWorldLight.ApplySwitches();
            WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasOn = true, On = true, HasClock = true, Clock = "pinned", HasTime = true,
                                                                           Time = "12:00:00" });
            WmvWorldLight.Frame(F + 4, 0.016, s, true, x, y, zz, origin, true);
            List<string> census = said.FindAll(l => l.StartsWith("world light: fog for map ", StringComparison.Ordinal));
            string wowCensus = census.Find(l => l.Contains("(product wow)")) ?? "";
            string eraCensus = census.Find(l => l.Contains("(product wow_classic_era)")) ?? "";
            string state = said.FindLast(l => l.StartsWith("world light: zone 139 light 204", StringComparison.Ordinal)) ?? "";
            string eraState = said.Find(l => l.StartsWith("world light: zone 139", StringComparison.Ordinal) && l.Contains("fog: off (")) ?? "";
            // Step 7 after in-game test T7: the end is E = min(0.933 x far clip, horizonStart) (this object draws no polynomial
            // path), horizonStart the switch's, and the clause names the far clip, horizonStart and E.
            var inv = System.Globalization.CultureInfo.InvariantCulture;
            double endE = WorldFog.EndOf(WmvWorldLight.FarClip, WmvWorldLight.HorizonStart);
            string end = " /yd, end " + endE.ToString("0.#", inv) + " yd, colour (";
            string endRule = ", E " + endE.ToString("0.#", inv) + " yd = min(" + WorldFog.EndFarClipFactor.ToString("0.###", inv) + " x far clip " +
                             WmvWorldLight.FarClip.ToString("0.#", inv) + ", horizonStart " + WmvWorldLight.HorizonStart.ToString("0.#", inv) + ")";
            bool endPacked = want.End == endE && want.EndRule == endE && want.FarClip == WmvWorldLight.FarClip &&
                             want.HorizonStart == sw.HorizonStart && WmvWorldLight.HorizonStart == sw.HorizonStart && !want.Artistic;
            bool lines = censusBefore == 3 && census.Count == 3 &&
                         wowCensus.Contains("fog for map 1 (product wow): " + WorldFog.ModeName(mode) + "; keys: E 3, L 0, Z 0 ") &&
                         wowCensus.Contains("2 day(s), 3 key(s)") && eraCensus.Contains(": " + WorldFog.ModeName(eraMode) + "; ") &&
                         (fog ? state.Contains("; fog: start ") && state.Contains(end) && state.Contains(endRule) && endPacked &&
                                describe.Contains("; fog: the client's per-pixel fog") && describe.Contains("horizonStart) = min(")
                              : state.Contains("; fog: off (")) &&
                         (eraMode != WorldFogMode.Off || !sw.Fog || eraState.Contains("fog: off (product wow_classic_era, D17/D18)"));
            var problems = new List<string>();
            foreach (string l in said)
                if (System.Text.RegularExpressions.Regex.IsMatch(l, @"\b(warning|error|exception)\b", System.Text.RegularExpressions.RegexOptions.IgnoreCase))
                    problems.Add(l);
            Check(nullOk && lines && problems.Count == 0,
                  "outdoor fog F13: a null set (no scene) neither throws nor fails the light off and shows nothing; the fog census is said for " +
                  "each new object while the light is on (" + censusBefore + " line(s) for wow, Era, wow again; " + census.Count + " after ApplySwitches " +
                  "and the same object: \"" + wowCensus + "\"); " +
                  "the state line carries this frame's fog, its end E = min(0.933 x far clip, horizonStart) with both inputs named and the " +
                  "packed record's end E (\"" + (state.IndexOf("; fog", StringComparison.Ordinal) >= 0
                      ? state.Substring(state.IndexOf("; fog", StringComparison.Ordinal)) : state) + "\"); no fog line says warning, error or exception" +
                  (problems.Count > 0 ? " -- " + problems[0] : ""), log);

            // The light tests' own object (three colours and FogDensity sent): its fog columns would read 0 in every key, so it is
            // lit but draws no fog -- no fog global written (sentinels kept), both slots 0 -- and the state line and the capture
            // line name the columns.
            WorldLightSet bare = WmvIpcClient.ParseWorldLight(WorldLightLine(""));
            string bareAbsent = WorldFog.AbsentColumns(bare, WorldFogMode.RetailPattern) ?? "";
            string[] bareNames = bareAbsent.Split(new[] { ", " }, StringSplitOptions.RemoveEmptyEntries);
            Shader.SetGlobalVector(WmvWorldLight.FogRowIds[0], sentinelRow);
            Shader.SetGlobalVector(WmvWorldLight.EyeId, sentinelEye);
            WmvWorldLight.Frame(F + 6, 0.016, bare, true, x, y, zz, origin, true);
            bool bareKept = Shader.GetGlobalVector(WmvWorldLight.FogRowIds[0]) == sentinelRow && Shader.GetGlobalVector(WmvWorldLight.EyeId) == sentinelEye;
            bool bareSlots = WmvWorldLight.PublishedOn && WmvWorldLight.ObjectsLit && Shader.GetGlobalVector(WmvWorldLight.TerrainId).z == 0f &&
                             Shader.GetGlobalVector(WmvWorldLight.ModelsId).w == 0f && !WmvWorldLight.FogDrawn && WmvWorldLight.FogMode == WorldFogMode.Off;
            string bareState = said.FindLast(l => l.StartsWith("world light: zone 139 light 204", StringComparison.Ordinal)) ?? "";
            string bareDescribe = WmvWorldLight.Describe();
            bool bareSaid = bareState.Contains("; fog: off (fog columns absent: " + bareAbsent + ")") &&
                            bareDescribe.Contains("; fog off for product wow (fog columns absent: " + bareAbsent + ")");
            bool bareList = bareNames.Length == 28 && bareNames[0] == "SkyFogColor" && bareNames[4] == "EndFogHeightColor" && bareNames[5] == "FogScaler" &&
                            Array.IndexOf(bareNames, "FogDensity") < 0 && Array.IndexOf(bareNames, "FogEnd") < 0;
            Check(bareList && bareKept && bareSlots && (!sw.Fog || bareSaid),
                  "outdoor fog F13: an object whose fog columns are not sent (the light tests' own: three colours and FogDensity) is lit but draws no " +
                  "fog -- " + bareNames.Length + " columns absent (the five fog colours and every fog float but FogDensity; FogEnd is the arms' only), " +
                  "the values written into _WmvWLFogR0 and _WmvWLEye before its frame unchanged, _WmvWLTerrain.z and _WmvWLModels.w 0; the state " +
                  "line and the capture line say so (\"" + (bareDescribe.IndexOf("; fog", StringComparison.Ordinal) >= 0
                      ? bareDescribe.Substring(bareDescribe.IndexOf("; fog", StringComparison.Ordinal)) : bareDescribe) + "\")", log);

            // ---- F15: no allocation in Publish (100 renders of the same frame), where the runtime counts allocations per thread
            long calibrate = GC.GetAllocatedBytesForCurrentThread();
            var probe = new byte[4096];
            long counted = GC.GetAllocatedBytesForCurrentThread() - calibrate;
            GC.KeepAlive(probe);
            if (counted < 4096)
                log("lifecycle-test SKIP: outdoor fog F15 (no allocation in Publish): this runtime does not count allocations per thread (a 4 KB " +
                    "array counted " + counted + " bytes)");
            else
            {
                WmvWorldLight.Frame(F + 5, 0.016, s, true, x, y, zz, origin, true);
                long before = GC.GetAllocatedBytesForCurrentThread();
                for (int k = 0; k < 100; k++)
                    WmvWorldLight.Frame(F + 5, 0.016, s, true, x, y, zz, origin, true);
                long spent = GC.GetAllocatedBytesForCurrentThread() - before;
                Check(spent == 0 && WmvWorldLight.FogDrawn == fog,
                      "outdoor fog F15: 100 publishes of a shown frame (the fog's 14 rows and the eye among them) allocate nothing (" + spent + " bytes)", log);
            }
        }
        finally
        {
            WmvWorldLight.ApplySwitches();
            WmvWorldLight.Off();
            WmvWorldLight.SetEye(savedEye);
            WmvWorldLight.Log = savedLog;
        }
    }
}
