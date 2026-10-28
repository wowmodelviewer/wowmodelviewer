// WmvLifecycleSelfTestWaterLight.cs
//
// The lifecycle self-test's WATER WITH THE LIGHT (Stage 10 step 8, docs/world-viewer/10-light-sky-fog.md 6.11, 13.3; the CPU
// twins are Wow/World/LiquidWaterDay.cs, SkyDome.Trace and LiquidHighMath.MixFogged / FogPremultiplied, the GPU's
// Resources/WmvLiquid.shader and WmvWaterSky.hlsl): OutdoorWaterTests, called by OutdoorLightTests after the fog's tests, and
// OutdoorWaterHardOffTests in its WMV_LIGHT=off branch. The GPU cases draw inside WmvTerrainComposite's render in float32
// (_WmvWLOn pinned 0 by the composite unless the case's own command buffer sets it, _WmvShaderEncode 0):
//   W1  the water sky probe (Resources/WmvWaterSkyProbe.shader: WmvWaterSkyDir over a 96 x 48 grid of directions, every fourth
//       column on a wedge edge, the first and last rows the poles) against SkyDome.Trace on the dome's own arrays, tables A and
//       B, within 3e-3 (the sky fixture's gate), with the inputs WmvWorldLight.PublishWaterSky publishes
//   W2  the HIGH water (the liquids' HIGH fixture: top-down, flat normals, a reflection that misses) with the light on: inside
//       the fog start (visibility 1), the arm off, the fog not drawn (_WmvWLModels.w 0) and an interior group's liquid
//       (_WmvLiquidUnfogged 1 by a MaterialPropertyBlock, the dome on: neither its fog nor the dome) each bit for bit the
//       light-off output; far into the fog the surface arm and the composite arm against MixFogged, on the miss fixture and on
//       the liquids' H2 hit (the reflected texels' copies, blur and sharp, left unfogged by the surface arm), the surface arm
//       with the encode on (_WmvLinearOut 1: W, the sky and the fog colour linear); a miss with the dome on taking
//       SkyDome.Trace's colour straight up
//   W3  the zone and fallback paths with the light on far into the fog against FogPremultiplied (the alpha carried)
//   W4  the driver's water globals (WmvLiquidLight on a small light object and a worldLight object of its lights): on the
//       clock at 12:00 bit for bit Stage 09's, at 09:00 the table's (a clock jump snaps), back on the noon rows Stage 09's;
//       the source lines said once each; the step-8 switches by default
//   W4b the wiring as WmvMain takes it (WmvLiquidLight.FrameAfterLight after WmvWorldLight.Frame on that object): the host's
//       pinned 09:00 gives the table's values, its 10:00 a clock jump the water snaps to; the outdoor light's fault path in the
//       same frame takes it again on the noon rows (Stage 09's values) and nothing more after; the light switched off: noon
//   W5  WMV_LIGHT=off: no water global written by the driver's frames (sentinels kept), no clock jump, never on the clock
// The interior liquids' MaterialPropertyBlock through the real binding is checked in the liquids' T15 (WmvLifecycleSelfTestLiquids).
// Every global touched is saved and put back.

using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;
using Wmv.Wow;
using Wmv.Wow.World;

public static partial class WmvLifecycleSelfTest
{
    const double WaterGate = 3e-3;
    static readonly int WaterSkyProbeGridId = Shader.PropertyToID("_WmvWaterSkyProbeGrid");

    static void OutdoorWaterTests(Action<string> log)
    {
        try { OutdoorWaterSkyProbeTests(log); }
        catch (Exception e) { Check(false, "water W1: the test threw " + e.GetType().Name + ": " + e.Message, log); }
        try { OutdoorWaterGpuTests(log); }
        catch (Exception e) { Check(false, "water W2-W3: the test threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log); }
        try { OutdoorWaterDriverTests(log); }
        catch (Exception e) { Check(false, "water W4: the test threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log); }
        finally { WmvLiquidLight.Off(); }
        try { OutdoorWaterWiringTests(log); }
        catch (Exception e) { Check(false, "water W4b: the test threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log); }
        finally { WmvLiquidLight.Off(); }
    }

    // ---------------------------------------------------------------- W1: the water sky probe

    static void OutdoorWaterSkyProbeTests(Action<string> log)
    {
        Shader shader = Resources.Load<Shader>("WmvWaterSkyProbe");
        if (shader == null)
            shader = Shader.Find("Wmv/WaterSkyProbe");
        if (shader == null || !shader.isSupported)
        {
            Check(false, "water W1: Resources/WmvWaterSkyProbe.shader is in the player build and supported", log);
            return;
        }
        var made = new List<UnityEngine.Object>();
        try
        {
            var m = new Material(shader) { name = "WmvWaterSkyProbe test", hideFlags = HideFlags.HideAndDontSave };
            made.Add(m);
            const int W = 96, H = 48;
            var quad = new Mesh { name = "WmvWaterSkyProbe quad" };
            made.Add(quad);
            quad.SetVertices(new List<Vector3> { new Vector3(0f, 50f, 0f), new Vector3(W, 50f, 0f), new Vector3(W, 50f, -H), new Vector3(0f, 50f, -H) });
            quad.SetIndices(new[] { 0, 1, 2, 0, 2, 3 }, MeshTopology.Triangles, 0);
            quad.RecalculateBounds();
            SkyInputs s = SkyFixtureInputs();
            foreach (bool tableB in new[] { false, true })
            {
                float[] p, slots;
                int[] ix;
                WmvWorldSky.DomeArrays(tableB, out p, out slots, out ix);
                WmvWorldLight.PublishWaterSky(s, tableB);
                float[] px = WmvTerrainComposite.RenderToArray(cmd =>
                {
                    cmd.SetGlobalVector(WaterSkyProbeGridId, new Vector4(W, H, 0f, 0f));
                    cmd.DrawMesh(quad, Matrix4x4.identity, m, 0, 0);
                }, 0f, 0f, W, H, 40f, 60f, W, H, false, false, true);
                double worst = 0.0;
                int missed = 0, nonFinite = 0;
                string at = "";
                for (int r = 0; r < H; r++)
                    for (int c = 0; c < W; c++)
                    {
                        double lon = 2.0 * Math.PI * c / W, e = Math.PI * r / (H - 1) - Math.PI / 2.0;
                        double dx = Math.Cos(e) * Math.Cos(lon), dy = Math.Sin(e), dz = Math.Cos(e) * Math.Sin(lon);
                        double rr, gg, bb;
                        if (!SkyDome.Trace(p, slots, ix, dx, dy, dz, s, out rr, out gg, out bb))
                        {
                            missed++;
                            continue;
                        }
                        int i = 4 * (r * W + c);
                        if (float.IsNaN(px[i]) || float.IsInfinity(px[i]))
                            nonFinite++;
                        double err = Math.Max(Math.Abs(px[i] - rr), Math.Max(Math.Abs(px[i + 1] - gg), Math.Abs(px[i + 2] - bb)));
                        if (err > worst || double.IsNaN(err))
                        {
                            worst = double.IsNaN(err) ? double.PositiveInfinity : err;
                            at = "column " + c + " row " + r;
                        }
                    }
                Check(worst <= WaterGate && missed == 0 && nonFinite == 0,
                      string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                    "water W1, table {0}: the GPU's WmvWaterSkyDir (one 15-deg wedge) against SkyDome.Trace (240 triangles) over {1} directions " +
                                    "(every fourth column on a wedge edge, the poles): worst channel {2:0.#######}{3} ({4}); {5} rays missed, {6} not finite",
                                    tableB ? "b" : "a", W * H, worst, at.Length > 0 ? " at " + at : "", WaterGate, missed, nonFinite), log);
            }
        }
        finally
        {
            foreach (UnityEngine.Object o in made)
                if (o != null)
                    UnityEngine.Object.DestroyImmediate(o);
        }
    }

    // ---------------------------------------------------------------- W2, W3: the water on the GPU with the light on

    /// <summary>What a lit render sets after the composite's pin: _WmvWLOn 1, the fog drawn or not (_WmvWLModels.w), the water
    /// fog's arm, the record and the eye, the dome on or not.</summary>
    sealed class WaterLit
    {
        public bool FogDrawn = true;
        public int Arm = LiquidHighMath.WaterFogSurface;
        public WorldFogRecord Record;
        public Vector3 Eye;
        public bool SkyOn;

        public void Apply(CommandBuffer cmd)
        {
            cmd.SetGlobalFloat(WmvWorldLight.OnId, 1f);
            cmd.SetGlobalVector(WmvWorldLight.ModelsId, new Vector4(0f, 0f, 0f, FogDrawn ? 1f : 0f));
            cmd.SetGlobalFloat(WmvWorldLight.WaterFogId, Arm);
            if (Record != null)
                FogRows(cmd, Record);
            cmd.SetGlobalVector(WmvWorldLight.EyeId, new Vector4(Eye.x, Eye.y, Eye.z, 1f));
            cmd.SetGlobalFloat(WmvWorldLight.WaterSkyOnId, SkyOn ? 1f : 0f);
        }
    }

    /// <summary>The liquids' HIGH fixture's water quad (HighRender's) drawn with `m`, the light set up by `lit` (null: off, the
    /// composite's pin) and the renderer's block `block` (null: none); `blur` and `refl` the reflections' copies (null: `colour`).</summary>
    static float[] HighRenderLit(HighFixture f, Material m, Texture colour, Texture depth, Vector4 sky, WaterLit lit, MaterialPropertyBlock block,
                                 out string why, Texture blur = null, Texture refl = null)
    {
        why = null;
        var mesh = new Mesh { name = "WmvLiquid step-8 test quad" };
        try
        {
            var uv1 = new Vector2(1f, -1f);
            mesh.SetVertices(new List<Vector3> { new Vector3(0f, 50f, 0f), new Vector3(8f, 50f, 0f), new Vector3(8f, 50f, 8f), new Vector3(0f, 50f, 8f) });
            mesh.SetUVs(0, new List<Vector2> { new Vector2(0f, 0f), new Vector2(1f, 0f), new Vector2(1f, 1f), new Vector2(0f, 1f) });
            mesh.SetUVs(1, new List<Vector2> { uv1, uv1, uv1, uv1 });
            mesh.SetIndices(new[] { 0, 2, 1, 0, 3, 2 }, MeshTopology.Triangles, 0);
            mesh.RecalculateBounds();
            return WmvTerrainComposite.RenderToArray(cmd =>
            {
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneColorId, colour);
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneColorReflId, refl != null ? refl : colour);
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneColorBlurId, blur != null ? blur : colour);
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneDepthId, depth);
                cmd.SetGlobalVector(WmvWaterGrabPass.GrabTexelId, new Vector4(0.25f, 0.25f, 4f, 4f));
                cmd.SetGlobalMatrix(InvVpId, f.InvVp);
                cmd.SetGlobalVector(WmvLiquidMaterials.ReflectSkyId, sky);
                cmd.SetGlobalFloat(WmvLiquidMaterials.LiquidTimeId, 0f);
                cmd.SetGlobalVector(WmvLiquidMaterials.RippleOriginId, Vector4.zero);
                cmd.SetGlobalFloat(WmvWaterGrabPass.WaterGrabId, 1f);
                if (lit != null)
                    lit.Apply(cmd);
                cmd.DrawMesh(mesh, Matrix4x4.identity, m, 0, 0, block);
                cmd.SetGlobalFloat(WmvWaterGrabPass.WaterGrabId, 0f);
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneColorId, Texture2D.blackTexture);
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneColorReflId, Texture2D.blackTexture);
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneColorBlurId, Texture2D.blackTexture);
                cmd.SetGlobalTexture(WmvWaterGrabPass.SceneDepthId, Texture2D.blackTexture);
            }, 0f, 8f, 8f, 8f, 40f, 60f, 4, 4, false, false, true);
        }
        catch (Exception e)
        {
            why = e.GetType().Name + ": " + e.Message;
            return null;
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(mesh);
        }
    }

    /// <summary>The liquids' zone-light quad (LiquidQuadRender's) with the light set up by `lit`: pixel (1, 1)'s rgba.</summary>
    static float[] LiquidQuadRenderLit(Material m, Vector2 uv1, WaterLit lit, out string why)
    {
        why = null;
        var mesh = new Mesh { name = "WmvLiquid step-8 zone test quad" };
        try
        {
            mesh.SetVertices(new List<Vector3> { new Vector3(0f, 50f, 0f), new Vector3(8f, 50f, 0f), new Vector3(8f, 50f, 8f), new Vector3(0f, 50f, 8f) });
            mesh.SetUVs(0, new List<Vector2> { new Vector2(0f, 0f), new Vector2(1f, 0f), new Vector2(1f, 1f), new Vector2(0f, 1f) });
            mesh.SetUVs(1, new List<Vector2> { uv1, uv1, uv1, uv1 });
            mesh.SetIndices(new[] { 0, 2, 1, 0, 3, 2 }, MeshTopology.Triangles, 0);
            mesh.RecalculateBounds();
            float[] px = WmvTerrainComposite.RenderToArray(cmd =>
            {
                if (lit != null)
                    lit.Apply(cmd);
                cmd.DrawMesh(mesh, Matrix4x4.identity, m, 0, 0);
            }, 0f, 8f, 8f, 8f, 40f, 60f, 4, 4, false, false, true);
            int at = (1 * 4 + 1) * 4;
            return new[] { px[at], px[at + 1], px[at + 2], px[at + 3] };
        }
        catch (Exception e)
        {
            why = e.GetType().Name + ": " + e.Message;
            return null;
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(mesh);
        }
    }

    static int SameBits(float[] a, float[] b)
    {
        if (a == null || b == null || a.Length != b.Length)
            return -1;
        int differ = 0;
        for (int k = 0; k < a.Length; k++)
            if (BitConverter.ToInt32(BitConverter.GetBytes(a[k]), 0) != BitConverter.ToInt32(BitConverter.GetBytes(b[k]), 0))
                differ++;
        return differ;
    }

    /// <summary>The shader's WmvAuthoredToLinear (the sRGB curve) on one channel.</summary>
    static double AuthoredToLinear(double c)
    {
        return c >= 0.04045 ? Math.Pow(Math.Max((c + 0.055) / 1.055, 0.0), 2.4) : c / 12.92;
    }

    /// <summary>The fog at the water point of pixel (column c, north-first row i) of the HIGH fixture (x 2c + 1, z 7 - 2i, y 50)
    /// seen from `eye`: visibility and colour (WorldFog's twin, authored).</summary>
    static void WaterFogAtPixel(WorldFogRecord r, Vector3 eye, int c, int i, out double vis, out double[] fogC)
    {
        double[] p = { 2 * c + 1 - eye.x, 50.0 - eye.y, 7 - 2 * i - eye.z };
        double hf;
        WorldFog.Visibility(r, p, FogUp, out vis, out hf);
        fogC = new double[3];
        WorldFog.Colour(r, p, FogUp, hf, fogC);
    }

    static void OutdoorWaterGpuTests(Action<string> log)
    {
        Shader shader = WmvLiquidMaterials.ResolveShader(null);
        if (shader == null)
        {
            Check(false, "water W2: the liquid shader is in the build", log);
            return;
        }
        LiquidHighTune tune = WmvLiquidMaterials.Tune;
        var f = new HighFixture();
        var made = new List<UnityEngine.Object>();
        Func<Texture2D, Texture2D> keep = t => { made.Add(t); return t; };
        // Every global a lit render touches, saved (the composite puts _WmvWLOn back itself).
        Vector4 savedModels = Shader.GetGlobalVector(WmvWorldLight.ModelsId), savedEye = Shader.GetGlobalVector(WmvWorldLight.EyeId);
        float savedWaterFog = Shader.GetGlobalFloat(WmvWorldLight.WaterFogId), savedSkyOn = Shader.GetGlobalFloat(WmvWorldLight.WaterSkyOnId);
        var savedRows = new Vector4[WorldFogRecord.Rows];
        for (int k = 0; k < savedRows.Length; k++)
            savedRows[k] = Shader.GetGlobalVector(WmvWorldLight.FogRowIds[k]);
        float savedTime = Shader.GetGlobalFloat(WmvLiquidMaterials.LiquidTimeId);
        try
        {
            WmvLiquidLight.Off();
            Material m = WmvLiquidMaterials.CreateMaterial(shader, new LiquidTypeView { Id = 1, Name = "Water", Kind = 0, Material = 1, MaterialFlags = 1, Lvf = 0,
                                                                                       MinimapColor = 0x9F12A0D5 });
            made.Add(m);
            m.SetFloat(WmvLiquidMaterials.LinearOutId, 0f);
            m.SetFloat(WmvLiquidMaterials.HasBumpId, 0f);
            m.SetFloat(WmvLiquidMaterials.HiReflBroadId, 1f);
            m.SetFloat(WmvLiquidMaterials.HiReflDetailId, 0f);
            m.SetFloat(WmvLiquidMaterials.HiF0Id, 0.3f);
            const double depthBelow = 1.0, rb = 0.3;
            Func<int, int, Color32> bedTexel = (col, row) => new Color32((byte)(40 + 50 * col), (byte)(30 + 60 * row), (byte)(200 - 30 * col - 20 * row), 255);
            Texture2D bed = keep(HighColour(bedTexel));
            Texture2D under = keep(HighDepth((col, row) => f.DepthOf(50f - (float)depthBelow)));
            var sky = new Vector4(0.9f, 0.6f, 0.3f, 1f);
            double[] baseColour = { 18 / 255.0, 160 / 255.0, 213 / 255.0 };
            double T = LiquidHighMath.Transmission(depthBelow, tune[LiquidHighTune.TFastA], tune[LiquidHighTune.TFastL], tune[LiquidHighTune.TSlowA],
                                                   tune[LiquidHighTune.TSlowL]);
            WorldFogRecord rec = FogBaseRecord();
            var near = new Vector3(4f, 60f, 4f);
            var far = new Vector3(4f - 1500f, 50f, 4f);
            string why;

            float[] off = HighRenderLit(f, m, bed, under, sky, null, null, out why);
            float[] inside = HighRenderLit(f, m, bed, under, sky, new WaterLit { Record = rec, Eye = near }, null, out why);
            float[] armOff = HighRenderLit(f, m, bed, under, sky, new WaterLit { Record = rec, Eye = far, Arm = LiquidHighMath.WaterFogOff }, null, out why);
            float[] notDrawn = HighRenderLit(f, m, bed, under, sky, new WaterLit { Record = rec, Eye = far, FogDrawn = false }, null, out why);
            SkyInputs skyIn = SkyFixtureInputs();
            WmvWorldLight.PublishWaterSky(skyIn, false);
            var block = new MaterialPropertyBlock();
            block.SetFloat(WmvWorldModels.LiquidUnfoggedId, 1f);
            float[] interior = HighRenderLit(f, m, bed, under, sky, new WaterLit { Record = rec, Eye = far, SkyOn = true }, block, out why);
            int dInside = SameBits(off, inside), dArmOff = SameBits(off, armOff), dNotDrawn = SameBits(off, notDrawn), dInterior = SameBits(off, interior);
            Check(off != null && dInside == 0 && dArmOff == 0 && dNotDrawn == 0 && dInterior == 0,
                  "water W2: the HIGH water with the light on is the light-off output bit for bit inside the fog start (visibility 1), with " +
                  "-wmvWaterFog=off, with the fog not drawn (_WmvWLModels.w 0) and for an interior group's liquid (_WmvLiquidUnfogged 1, far into " +
                  "the fog with the dome on: neither its fog nor the dome sky) -- differing " +
                  "values " + dInside + ", " + dArmOff + ", " + dNotDrawn + ", " + dInterior + " of 64" + (why != null ? " (" + why + ")" : ""), log);

            // Far into the fog: the surface arm and the composite arm against the twin (a miss: the sky; flat normals: Fresnel f0).
            foreach (int arm in new[] { LiquidHighMath.WaterFogSurface, LiquidHighMath.WaterFogComposite })
            {
                float[] px = HighRenderLit(f, m, bed, under, sky, new WaterLit { Record = rec, Eye = far, Arm = arm }, null, out why);
                double worst = 0.0, minVis = 1.0;
                for (int i = 0; i < 4 && px != null; i++)
                    for (int c = 0; c < 4; c++)
                    {
                        double vis;
                        double[] fogC;
                        WaterFogAtPixel(rec, far, c, i, out vis, out fogC);
                        minVis = Math.Min(minVis, vis);
                        Color32 b = bedTexel(c, 3 - i);
                        for (int k = 0; k < 3; k++)
                        {
                            double bedK = (k == 0 ? b.r : k == 1 ? b.g : b.b) / 255.0;
                            double W = LiquidHighMath.WaterNoZone(baseColour[k], tune[LiquidHighTune.NoZoneGain]);
                            double want = LiquidHighMath.MixFogged(W, bedK, T, sky[k], rb, 0.0, false, 0, 0, 0, 0, 0, vis, fogC[k], arm);
                            worst = Math.Max(worst, Math.Abs(px[(i * 4 + c) * 4 + k] - want));
                        }
                        worst = Math.Max(worst, Math.Abs(px[(i * 4 + c) * 4 + 3] - 1.0));
                    }
                int differ = SameBits(off, px);
                Check(px != null && worst <= WaterGate && minVis < 0.9 && differ > 0,
                      string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                    "water W2: far into the fog (visibility down to {0:0.000}) the HIGH water's {1} arm against LiquidHighMath.MixFogged: worst " +
                                    "channel {2:0.######} ({3}); {4} of 64 values moved from the light-off output{5}", minVis,
                                    arm == LiquidHighMath.WaterFogSurface ? "surface" : "composite", worst, WaterGate, differ,
                                    why != null ? " (" + why + ")" : ""), log);
            }

            // The encode (_WmvLinearOut 1, as _WmvShaderEncode in WMV_DISPLAY=fragment): W, the sky and the fog colour converted to
            // linear before the mix, the grab's bed as it is -- the surface arm far into the fog against MixFogged on those values.
            m.SetFloat(WmvLiquidMaterials.LinearOutId, 1f);
            float[] enc = HighRenderLit(f, m, bed, under, sky, new WaterLit { Record = rec, Eye = far }, null, out why);
            m.SetFloat(WmvLiquidMaterials.LinearOutId, 0f);
            double encWorst = 0.0, encMinVis = 1.0;
            for (int i = 0; i < 4 && enc != null; i++)
                for (int c = 0; c < 4; c++)
                {
                    double vis;
                    double[] fogC;
                    WaterFogAtPixel(rec, far, c, i, out vis, out fogC);
                    encMinVis = Math.Min(encMinVis, vis);
                    Color32 b = bedTexel(c, 3 - i);
                    for (int k = 0; k < 3; k++)
                    {
                        double bedK = (k == 0 ? b.r : k == 1 ? b.g : b.b) / 255.0;
                        double W = AuthoredToLinear(LiquidHighMath.WaterNoZone(baseColour[k], tune[LiquidHighTune.NoZoneGain]));
                        double want = LiquidHighMath.MixFogged(W, bedK, T, AuthoredToLinear(sky[k]), rb, 0.0, false, 0, 0, 0, 0, 0, vis, AuthoredToLinear(fogC[k]),
                                                               LiquidHighMath.WaterFogSurface);
                        encWorst = Math.Max(encWorst, Math.Abs(enc[(i * 4 + c) * 4 + k] - want));
                    }
                }
            Check(enc != null && encWorst <= WaterGate && encMinVis < 0.9,
                  string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                "water W2: with the encode on (_WmvLinearOut 1) far into the fog (visibility down to {0:0.000}) the surface arm mixes W, the " +
                                "sky and the fog colour converted to linear with the grab's bed as it is (MixFogged on those): worst channel {1:0.######} ({2})" +
                                "{3}", encMinVis, encWorst, WaterGate, why != null ? " (" + why + ")" : ""), log);

            // The hit branch: the liquids' H2 fixture (the ripple map tilts the reflected ray east; from column 1 it passes under a
            // surface 0.6 yd above the water and hits, from column 0 it rises above it and misses), broad 1 + detail 1 at f0 1 (the
            // shares 0.5 + 0.5, the refraction's 0), the blur and the sharp copies each a texel partly covered by the scene: far into
            // the fog both arms against MixFogged with the hit, the edge fade 1 and each copy's coverage -- in the surface arm the
            // copies' colour (the scene terms) is never fogged, only the sky's share of them.
            Material hm = WmvLiquidMaterials.CreateMaterial(shader, new LiquidTypeView { Id = 1, Name = "Water", Kind = 0, Material = 1, MaterialFlags = 1, Lvf = 0,
                                                                                        MinimapColor = 0x9F12A0D5 });
            made.Add(hm);
            hm.SetFloat(WmvLiquidMaterials.LinearOutId, 0f);
            hm.SetTexture(WmvLiquidMaterials.BumpMapId, keep(HighColour((col, row) => new Color32(204, 128, 230, 255))));
            hm.SetFloat(WmvLiquidMaterials.HasBumpId, 1f);
            hm.SetFloat(WmvLiquidMaterials.HiBumpStrengthId, 1f);
            hm.SetVector(WmvLiquidMaterials.HiBumpMeanId, Vector4.zero);
            hm.SetFloat(WmvLiquidMaterials.HiRefractId, 0f);
            hm.SetFloat(WmvLiquidMaterials.HiReflBumpId, 1f);
            hm.SetVector(WmvLiquidMaterials.HiReflMarchId, new Vector4(128f, 0.3f, 0.02f, 0.08f));
            hm.SetFloat(WmvLiquidMaterials.HiReflBroadId, 1f);
            hm.SetFloat(WmvLiquidMaterials.HiReflDetailId, 1f);
            hm.SetFloat(WmvLiquidMaterials.HiF0Id, 1f);
            var green = new Color32(10, 250, 10, 255);
            var blurTexel = new Color32(100, 60, 20, 128);
            var sharpTexel = new Color32(30, 90, 50, 96);
            Texture2D hitScene = keep(HighColour((col, row) => col == 0 ? green : new Color32(200, 120, 40, 255)));
            Texture2D hitBlur = keep(HighColour((col, row) => col == 0 ? green : blurTexel));
            Texture2D hitSharp = keep(HighColour((col, row) => col == 0 ? green : sharpTexel));
            Texture2D hitDepth = keep(HighDepth((col, row) => f.DepthOf(col < 2 ? 49f : 50.6f)));
            var hitSky = new Vector4(1f, 0.5f, 0.25f, 1f);
            float[] hitOff = HighRenderLit(f, hm, hitScene, hitDepth, hitSky, null, null, out why, hitBlur, hitSharp);
            foreach (int arm in new[] { LiquidHighMath.WaterFogSurface, LiquidHighMath.WaterFogComposite })
            {
                float[] px = HighRenderLit(f, hm, hitScene, hitDepth, hitSky, new WaterLit { Record = rec, Eye = far, Arm = arm }, null, out why, hitBlur, hitSharp);
                double worst = 0.0, offWorst = 0.0, minVis = 1.0;
                for (int i = 0; i < 4 && px != null && hitOff != null; i++)
                    foreach (int c in new[] { 0, 1 })
                    {
                        double vis;
                        double[] fogC;
                        WaterFogAtPixel(rec, far, c, i, out vis, out fogC);
                        minVis = Math.Min(minVis, vis);
                        bool isHit = c == 1;
                        for (int k = 0; k < 3; k++)
                        {
                            double bc = (k == 0 ? blurTexel.r : k == 1 ? blurTexel.g : blurTexel.b) / 255.0;
                            double sc = (k == 0 ? sharpTexel.r : k == 1 ? sharpTexel.g : sharpTexel.b) / 255.0;
                            double want = LiquidHighMath.MixFogged(0.0, 0.0, 0.0, hitSky[k], 1.0, 1.0, isHit, 1.0, bc, blurTexel.a / 255.0, sc, sharpTexel.a / 255.0,
                                                                   vis, fogC[k], arm);
                            double want09 = LiquidHighMath.MixFogged(0.0, 0.0, 0.0, hitSky[k], 1.0, 1.0, isHit, 1.0, bc, blurTexel.a / 255.0, sc,
                                                                     sharpTexel.a / 255.0, 1.0, 0.0, LiquidHighMath.WaterFogOff);
                            int at = (i * 4 + c) * 4 + k;
                            worst = Math.Max(worst, Math.Abs(px[at] - want));
                            offWorst = Math.Max(offWorst, Math.Abs(hitOff[at] - want09));
                        }
                    }
                Check(px != null && hitOff != null && worst <= WaterGate && offWorst <= WaterGate && minVis < 0.9 && SameBits(hitOff, px) > 0,
                      string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                    "water W2: the hit branch (the liquids' H2 hit: column 1 hits copies half and 38% covered, column 0 misses) far into the " +
                                    "fog (visibility down to {0:0.000}), the {1} arm against MixFogged with the hit, fade 1 and each copy's coverage: worst " +
                                    "channel {2:0.######}, the light-off output against Stage 09's form {3:0.######} ({4}){5}", minVis,
                                    arm == LiquidHighMath.WaterFogSurface ? "surface" : "composite", worst, offWorst, WaterGate,
                                    why != null ? " (" + why + ")" : ""), log);
            }

            // The dome: a miss straight up (flat water, top-down) takes SkyDome.Trace's colour at (0, 1, 0), not _WmvReflectSky.
            WmvWorldLight.PublishWaterSky(skyIn, false);
            float[] dp;
            float[] dsl;
            int[] dix;
            WmvWorldSky.DomeArrays(false, out dp, out dsl, out dix);
            double sr, sg, sb;
            SkyDome.Trace(dp, dsl, dix, 0, 1, 0, skyIn, out sr, out sg, out sb);
            double[] dome = { sr, sg, sb };
            float[] domePx = HighRenderLit(f, m, bed, under, sky, new WaterLit { Record = rec, Eye = far, Arm = LiquidHighMath.WaterFogOff, SkyOn = true },
                                           null, out why);
            float[] domeOff = HighRenderLit(f, m, bed, under, sky, new WaterLit { Record = rec, Eye = far, Arm = LiquidHighMath.WaterFogOff, SkyOn = false },
                                            null, out why);
            double worstDome = 0.0;
            for (int i = 0; i < 4 && domePx != null; i++)
                for (int c = 0; c < 4; c++)
                {
                    Color32 b = bedTexel(c, 3 - i);
                    for (int k = 0; k < 3; k++)
                    {
                        double bedK = (k == 0 ? b.r : k == 1 ? b.g : b.b) / 255.0;
                        double W = LiquidHighMath.WaterNoZone(baseColour[k], tune[LiquidHighTune.NoZoneGain]);
                        double want = LiquidHighMath.MixFogged(W, bedK, T, dome[k], rb, 0.0, false, 0, 0, 0, 0, 0, 1.0, 0.0, LiquidHighMath.WaterFogOff);
                        worstDome = Math.Max(worstDome, Math.Abs(domePx[(i * 4 + c) * 4 + k] - want));
                    }
                }
            Check(domePx != null && worstDome <= WaterGate && SameBits(off, domeOff) == 0 && SameBits(off, domePx) > 0,
                  string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                "water W2: with _WmvWaterSkyOn 1 a reflection that misses takes the sky dome straight up ({0:0.000}, {1:0.000}, {2:0.000}: " +
                                "SkyDome.Trace), worst channel {3:0.######} ({4}); with it 0 the light-off output bit for bit (_WmvReflectSky)", sr, sg, sb,
                                worstDome, WaterGate), log);

            // W3: the zone and fallback paths with the light on, far into the fog, against FogPremultiplied (pixel (1, 1)).
            var frames = new Texture2DArray(4, 4, 1, TextureFormat.RGBA32, false, true) { name = "WmvLiquid step-8 test frame" };
            made.Add(frames);
            var texels = new Color32[16];
            for (int k = 0; k < texels.Length; k++)
                texels[k] = new Color32(0, 0, 0, 153);
            frames.SetPixels32(texels, 0);
            frames.Apply(false, false);
            Material zw = WmvLiquidMaterials.CreateMaterial(shader, new LiquidTypeView { Id = 1, Name = "Water", Kind = 0, Material = 1, MaterialFlags = 1, Lvf = 0,
                                                                                        MinimapColor = 0x9F12A0D5 });
            made.Add(zw);
            zw.SetTexture(WmvLiquidMaterials.FramesId, frames);
            zw.SetFloat(WmvLiquidMaterials.FrameCountId, 1f);
            zw.SetFloat(WmvLiquidMaterials.FrameId, 0f);
            zw.SetFloat(WmvLiquidMaterials.LinearOutId, 0f);
            double fa = 153 / 255.0, h0 = WmvLiquidMaterials.ZoneHighlight * Math.Min(1.0, Math.Max(0.0, (fa - WmvLiquidMaterials.ZoneHighlightFloor) /
                                                                                                  (1 - WmvLiquidMaterials.ZoneHighlightFloor)));
            double d14 = LiquidFormat.DepthYardsW(14), k14 = Math.Min(1.0, d14 / WmvLiquidMaterials.DepthFull), a14 = 0.4 + (0.8 - 0.4) * k14;
            double out14 = 1 - WmvLiquidMaterials.BedFactor * (1 - a14);
            double[] close = { 64, 62, 35 }, farC = { 62, 54, 47 };
            double fbA = WmvLiquidMaterials.ShallowAlpha + (WmvLiquidMaterials.DeepAlpha - WmvLiquidMaterials.ShallowAlpha) * (14 / 255.0);
            var depth14 = new Vector2(14f / 255f, (float)d14);
            double v11;
            double[] fog11;
            WaterFogAtPixel(rec, far, 1, 1, out v11, out fog11);
            WmvLiquidLight.Sample(LiquidTestLights(0), 50, 50, 0, 0.016, true);
            float[] zone = LiquidQuadRenderLit(zw, depth14, new WaterLit { Record = rec, Eye = far }, out why);
            WmvLiquidLight.Off();
            float[] fallback = LiquidQuadRenderLit(zw, depth14, new WaterLit { Record = rec, Eye = far }, out string why2);
            double zoneWorst = 0.0, fbWorst = 0.0;
            for (int k = 0; k < 3; k++)
            {
                double rgb = a14 * (close[k] + (farC[k] - close[k]) * k14) / 255 + h0;
                if (zone != null)
                    zoneWorst = Math.Max(zoneWorst, Math.Abs(zone[k] - LiquidHighMath.FogPremultiplied(rgb, out14, v11, fog11[k])));
                double fbRgb = Math.Min(1.0, baseColour[k] + fa * WmvLiquidMaterials.HighlightScale) * fbA;
                if (fallback != null)
                    fbWorst = Math.Max(fbWorst, Math.Abs(fallback[k] - LiquidHighMath.FogPremultiplied(fbRgb, fbA, v11, fog11[k])));
            }
            bool alphas = zone != null && fallback != null && Math.Abs(zone[3] - out14 * out14) <= WaterGate && Math.Abs(fallback[3] - fbA * fbA) <= WaterGate;
            Check(zone != null && fallback != null && zoneWorst <= WaterGate && fbWorst <= WaterGate && alphas && v11 < 0.9,
                  string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                "water W3: far into the fog (visibility {0:0.000}) the zone path and the fallback with the light on take vis rgb + (1 - vis) " +
                                "fogC alpha, the alpha carried (LiquidHighMath.FogPremultiplied): worst channel {1:0.######} and {2:0.######} ({3}); alphas " +
                                "{4} and {5}{6}", v11, zoneWorst, fbWorst, WaterGate, Rgba(zone), Rgba(fallback),
                                why != null || why2 != null ? " (" + why + " " + why2 + ")" : ""), log);
        }
        finally
        {
            WmvLiquidLight.Off();
            Shader.SetGlobalVector(WmvWorldLight.ModelsId, savedModels);
            Shader.SetGlobalVector(WmvWorldLight.EyeId, savedEye);
            Shader.SetGlobalFloat(WmvWorldLight.WaterFogId, savedWaterFog);
            Shader.SetGlobalFloat(WmvWorldLight.WaterSkyOnId, savedSkyOn);
            for (int k = 0; k < savedRows.Length; k++)
                Shader.SetGlobalVector(WmvWorldLight.FogRowIds[k], savedRows[k]);
            Shader.SetGlobalFloat(WmvLiquidMaterials.LiquidTimeId, savedTime);
            foreach (UnityEngine.Object o in made)
                if (o != null)
                    UnityEngine.Object.DestroyImmediate(o);
        }
    }

    // ---------------------------------------------------------------- W4: the driver's water globals

    /// <summary>A worldLight object for LiquidTestLights(0): lights 204 (LightParams 5985) and 191 (192) with the four water colours
    /// only, their keys at 12:00 the rows' noon bytes and at 06:00 others (204: (20, 40, 60) on every column; 191: (100, 90, 80)).</summary>
    static WorldLightSet WaterTestSet()
    {
        var s = new WorldLightSet { State = "ok", Map = 0, Product = "test", DefaultLight = 191 };
        s.SetColumns(new[] { "RiverCloseColor", "RiverFarColor", "OceanCloseColor", "OceanFarColor" }, new string[0], new string[0], new string[0]);
        Func<int, int, int, int> hex = (r, g, b) => (r << 16) | (g << 8) | b;
        int d204 = s.AddDay(new[] { 720, 1440 }, new[] { hex(20, 40, 60), hex(20, 40, 60), hex(20, 40, 60), hex(20, 40, 60),
                                                         hex(64, 62, 35), hex(62, 54, 47), hex(82, 90, 94), hex(55, 61, 69) }, new double[0], new long[0]);
        int d191 = s.AddDay(new[] { 720, 1440 }, new[] { hex(100, 90, 80), hex(100, 90, 80), hex(100, 90, 80), hex(100, 90, 80),
                                                         hex(88, 84, 69), hex(43, 67, 81), hex(16, 65, 77), hex(0, 29, 41) }, new double[0], new long[0]);
        s.AddParams(5985, d204, 0, 0, 0, 0, null, 0, 0);
        s.AddParams(192, d191, 0, 0, 0, 0, null, 0, 0);
        s.AddLight(204, new[] { 5985, 0, 0, 0, 0, 0, 0, 0 });
        s.AddLight(191, new[] { 192, 0, 0, 0, 0, 0, 0, 0 });
        return s;
    }

    static readonly int[] WaterGlobalIds =
    {
        WmvLiquidLight.RiverCloseId, WmvLiquidLight.RiverFarId, WmvLiquidLight.OceanCloseId, WmvLiquidLight.OceanFarId, WmvLiquidLight.WaterAlphaId,
        WmvLiquidLight.OceanAlphaId,
    };

    static Vector4[] WaterGlobals()
    {
        var v = new Vector4[WaterGlobalIds.Length + 1];
        for (int k = 0; k < WaterGlobalIds.Length; k++)
            v[k] = Shader.GetGlobalVector(WaterGlobalIds[k]);
        v[WaterGlobalIds.Length] = new Vector4(Shader.GetGlobalFloat(WmvLiquidLight.ZoneLightId), 0f, 0f, 0f);
        return v;
    }

    static bool SameGlobals(Vector4[] a, Vector4[] b)
    {
        for (int k = 0; k < a.Length; k++)
            for (int c = 0; c < 4; c++)
                if (BitConverter.ToInt32(BitConverter.GetBytes(a[k][c]), 0) != BitConverter.ToInt32(BitConverter.GetBytes(b[k][c]), 0))
                    return false;
        return true;
    }

    /// <summary>The globals the values `v` (LiquidLightSample's layout) give, as WmvLiquidLight sets them.</summary>
    static bool GlobalsAre(double[] v)
    {
        int[] at = { LiquidLightSample.RiverClose, LiquidLightSample.RiverFar, LiquidLightSample.OceanClose, LiquidLightSample.OceanFar };
        for (int k = 0; k < 4; k++)
        {
            Vector4 g = Shader.GetGlobalVector(WaterGlobalIds[k]);
            if (g.x != (float)v[at[k]] || g.y != (float)v[at[k] + 1] || g.z != (float)v[at[k] + 2])
                return false;
        }
        Vector4 wa = Shader.GetGlobalVector(WmvLiquidLight.WaterAlphaId), oa = Shader.GetGlobalVector(WmvLiquidLight.OceanAlphaId);
        return wa.x == (float)v[LiquidLightSample.WaterAlpha] && wa.y == (float)v[LiquidLightSample.WaterAlpha + 1] &&
               oa.x == (float)v[LiquidLightSample.OceanAlpha] && oa.y == (float)v[LiquidLightSample.OceanAlpha + 1] &&
               Shader.GetGlobalFloat(WmvLiquidLight.ZoneLightId) == 1f;
    }

    static void OutdoorWaterDriverTests(Action<string> log)
    {
        var said = new List<string>();
        Action<string> savedLog = WmvLiquidLight.Log;
        WmvLiquidLight.Log = x => said.Add(x);
        try
        {
            WmvLiquidLight.Off();
            LiquidLights l = LiquidTestLights(0);
            WorldLightSet s = WaterTestSet();
            WmvLiquidLight.Sample(l, 50, 50, 0, 0.016, true);                               // Stage 09: the noon rows (zone 139, light 204)
            Vector4[] stage09 = WaterGlobals();
            said.Clear();
            WmvLiquidLight.Sample(l, 50, 50, 0, 0.016, false, s, LiquidWaterDay.Noon, false, false);   // the clock at 12:00: a source snap
            Vector4[] clockNoon = WaterGlobals();
            bool noon = SameGlobals(stage09, clockNoon) && WmvLiquidLight.OnClock && WmvLiquidLight.WaterDay.FromDay == 2 &&
                        WmvLiquidLight.WaterDay.NoonFallbacks == 0 && said.Contains("liquid light: on the light's clock (t 12:00)");
            WmvLiquidLight.Sample(l, 50, 50, 0, 0.016, false, s, 1080, false, true);       // 09:00, the clock jumped: snapped to the table
            LiquidLightValues v204;
            bool atNine = WmvLiquidLight.WaterDay.Table.TryGetValue(204, out v204) && GlobalsAre(v204.V) && WmvLiquidLight.WaterDay.HalfMinute == 1080 &&
                          v204.V[LiquidLightSample.RiverClose] == WorldLightDay.Round(20, 64, 0.5) / 255.0;
            WmvLiquidLight.Sample(l, 50, 50, 0, 0.016, false, s, 1080, false, false);      // the same half-minute: nothing rebuilt
            bool cached = WmvLiquidLight.WaterDay.HalfMinute == 1080 && GlobalsAre(v204.V);
            int builds = WmvLiquidLight.WaterDay.Builds;
            WmvLiquidLight.Sample(l, 50, 50, 0, 0.016, false, s, 1080, false, false);
            cached &= WmvLiquidLight.WaterDay.Builds == builds;
            WmvLiquidLight.Sample(l, 50, 50, 0, 0.016, false);                             // back on the noon rows: a source snap
            Vector4[] back = WaterGlobals();
            bool backNoon = SameGlobals(stage09, back) && !WmvLiquidLight.OnClock && said.Contains("liquid light: Stage 09 noon");
            WmvLiquidLight.Sample(l, 101, 50, 0, 0.016, false, s, 1080, false, false);     // the map default 191 on the clock at 09:00
            LiquidLightValues v191;
            bool deflt = WmvLiquidLight.WaterDay.Table.TryGetValue(191, out v191) && GlobalsAre(v191.V) && WmvLiquidLight.Ease.Light == 191;
            WmvLiquidLight.Off();
            WorldLightSwitches sw = WmvWorldLight.Switches;
            bool switches = sw.WaterClock && sw.WaterSkyDome && sw.WaterFog == LiquidHighMath.WaterFogSurface || !sw.DrawDefaults;
            Check(noon && atNine && cached && backNoon && deflt && switches && !WmvLiquidLight.On,
                  "water W4: the water's globals on the light's clock -- at 12:00 Stage 09's bit for bit (the source change snaps, said once: \"liquid " +
                  "light: on the light's clock (t 12:00)\"), at 09:00 the table's (a clock jump snaps; the same half-minute builds nothing), back on the " +
                  "noon rows Stage 09's again (\"liquid light: Stage 09 noon\"), the map default on the clock; the step-8 switches clock, dome and " +
                  "surface unless an arm is given (" + noon + ", " + atNine + ", " + cached + ", " + backNoon + ", " + deflt + ", " + switches + "; " +
                  string.Join(" | ", said.ToArray()) + ")", log);
        }
        finally
        {
            WmvLiquidLight.Off();
            WmvLiquidLight.Log = savedLog;
        }
    }

    // ---------------------------------------------------------------- W4b: the wiring as WmvMain takes it

    /// <summary>W4b: WmvLiquidLight.FrameAfterLight -- what WmvMain.WorldLightFrame calls after WmvWorldLight.Frame, and with no
    /// object on its fault path -- driven by the outdoor light itself on WaterTestSet: the half-minute, the clamp arm, the clock jump
    /// and WaterOnClock come from WmvWorldLight, not from the test.</summary>
    static void OutdoorWaterWiringTests(Action<string> log)
    {
        if (!WmvWorldLight.Switches.WaterClock)
        {
            Check(true, "water W4b: skipped (-wmvWaterLight=noon given: the water never reads the light's clock)", log);
            return;
        }
        var said = new List<string>();
        Action<string> savedLog = WmvLiquidLight.Log, savedLightLog = WmvWorldLight.Log;
        WmvLiquidLight.Log = x => said.Add(x);
        WmvWorldLight.Log = null;
        try
        {
            WmvLiquidLight.Off();
            WmvWorldLight.Off();
            LiquidLights l = LiquidTestLights(0);
            WorldLightSet s = WaterTestSet();
            bool clamp = WmvWorldLight.Switches.Rules.Clamp;
            var want = new LiquidWaterDay();
            var s09 = new LiquidLightSample();
            l.Evaluate(50, 50, 0, s09);                                                  // Stage 09: zone 139, light 204
            const int F = -5200;                                                         // frame numbers Time.frameCount never takes
            // The host pins the clock at 09:00: the outdoor light's frame, then the water's as WorldLightFrame takes it.
            WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasOn = true, On = true, HasClock = true, Clock = "pinned", HasTime = true,
                                                                           Time = "09:00:00" });
            WmvWorldLight.Frame(F, 0.016, s, true, 50, 50, 0, 0, false);
            WmvLiquidLight.FrameAfterLight(F, 0.016, l, 50, 50, 0, false, s);
            want.Update(l, s, 1080, clamp);
            LiquidLightValues v;
            bool nine = WmvWorldLight.WaterOnClock && WmvLiquidLight.OnClock && WmvLiquidLight.WaterDay.HalfMinute == 1080 &&
                        want.Table.TryGetValue(204, out v) && GlobalsAre(v.V) && said.Contains("liquid light: on the light's clock (t 09:00)");
            // The host sets 10:00: a clock jump of this frame, which the water's ease takes at once ("clock": the same place, source
            // and validity, so nothing else snaps it; an eased step of 16 ms would not reach the new values).
            WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasTime = true, Time = "10:00:00" });
            WmvWorldLight.Frame(F + 1, 0.016, s, true, 50, 50, 0, 0, false);
            bool jumped = WmvWorldLight.ClockJumpedThisFrame;
            WmvLiquidLight.FrameAfterLight(F + 1, 0.016, l, 50, 50, 0, false, s);
            want.Update(l, s, 1200, clamp);
            bool ten = jumped && WmvLiquidLight.OnClock && WmvLiquidLight.WaterDay.HalfMinute == 1200 && want.Table.TryGetValue(204, out v) && GlobalsAre(v.V);
            // The outdoor light's fault path in the same frame (no object): the frame taken again on the noon rows -- Stage 09's values
            // at once (the source's snap) --; a second noon call and a clock call after it take nothing more.
            WmvLiquidLight.FrameAfterLight(F + 1, 0.016, l, 50, 50, 0, false, null);
            bool retaken = !WmvLiquidLight.OnClock && GlobalsAre(s09.V) && said.Contains("liquid light: Stage 09 noon");
            int builds = WmvLiquidLight.WaterDay.Builds;
            WmvLiquidLight.FrameAfterLight(F + 1, 0.016, l, 50, 50, 0, false, null);
            WmvLiquidLight.FrameAfterLight(F + 1, 0.016, l, 50, 50, 0, false, s);
            bool once = !WmvLiquidLight.OnClock && GlobalsAre(s09.V) && WmvLiquidLight.WaterDay.Builds == builds;
            // The host switches the light off: not shown, so the object is not read -- the noon rows.
            WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasOn = true, On = false });
            WmvWorldLight.Frame(F + 2, 0.016, s, true, 50, 50, 0, 0, false);
            WmvLiquidLight.FrameAfterLight(F + 2, 0.016, l, 50, 50, 0, false, s);
            bool offNoon = !WmvWorldLight.WaterOnClock && !WmvLiquidLight.OnClock && GlobalsAre(s09.V);
            Check(nine && ten && retaken && once && offNoon,
                  "water W4b: the wiring as WmvMain takes it (FrameAfterLight after WmvWorldLight.Frame) -- the host's pinned 09:00 gives the " +
                  "table's values at 1080; its 10:00 is a clock jump of the frame (ClockJumpedThisFrame) the water snaps to (1200); the fault " +
                  "path in that frame takes it again on the noon rows (Stage 09's values, once); the light switched off reads the noon rows (" +
                  nine + ", " + ten + ", " + retaken + ", " + once + ", " + offNoon + "; " + string.Join(" | ", said.ToArray()) + ")", log);
        }
        finally
        {
            WmvLiquidLight.Off();
            WmvLiquidLight.Log = savedLog;
            WmvWorldLight.ApplySwitches();
            WmvWorldLight.Off();
            WmvWorldLight.Log = savedLightLog;
        }
    }

    // ---------------------------------------------------------------- W5: WMV_LIGHT=off

    static void OutdoorWaterHardOffTests(Action<string> log)
    {
        float savedFog = Shader.GetGlobalFloat(WmvWorldLight.WaterFogId), savedSky = Shader.GetGlobalFloat(WmvWorldLight.WaterSkyOnId);
        Shader.SetGlobalFloat(WmvWorldLight.WaterFogId, 7.25f);
        Shader.SetGlobalFloat(WmvWorldLight.WaterSkyOnId, 5.5f);
        try
        {
            WorldLightSet s = WaterTestSet();
            WmvWorldLight.Frame(-3100, 0.016, s, true, 50, 50, 0, 0, false);
            WmvWorldLight.Sky(Camera.main, true);
            bool untouched = Shader.GetGlobalFloat(WmvWorldLight.WaterFogId) == 7.25f && Shader.GetGlobalFloat(WmvWorldLight.WaterSkyOnId) == 5.5f;
            Check(untouched && !WmvWorldLight.WaterOnClock && !WmvWorldLight.ClockJumpedThisFrame && !WmvWorldLight.WaterSkyOn,
                  "water W5: WMV_LIGHT=off -- the driver's frames write no water global (_WmvWaterFog, _WmvWaterSkyOn kept), the water is never on the " +
                  "light's clock and takes no clock jump: Stage 09's water", log);
        }
        finally
        {
            Shader.SetGlobalFloat(WmvWorldLight.WaterFogId, savedFog);
            Shader.SetGlobalFloat(WmvWorldLight.WaterSkyOnId, savedSky);
        }
    }
}
