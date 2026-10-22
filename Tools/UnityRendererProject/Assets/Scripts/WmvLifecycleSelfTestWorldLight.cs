// WmvLifecycleSelfTestWorldLight.cs
//
// The lifecycle self-test's OUTDOOR LIGHT (Stage 10 steps 3-5, protocol 20, docs/world-viewer/10-light-sky-fog.md 6.3-6.10),
// with a small object of the test's own (two LightData keys of Retail's LightParams 5985 and a white point light): the offer
// read the way the reader thread reads it ("worldLight" on a loadWorld line: its entries, a malformed zone dropped,
// "timeOverride" only when the line carries it -- 0, a positive and a negative value --, "unavailable", absent; the wire
// values the float32 and long fields exist for: 0.15 and 0.05 read as the float the client holds, flags and integers of
// 2^31 and more; the command's log text still ending with the liquids, and none of the outdoor light's text with
// WMV_LIGHT=off), setWorldLight's present fields (and the spec's "cmd" spelling), runtimeState's light fields (before the
// liquid counters, so the line still ends with them); then the driver (WmvWorldLight): its globals equal the pure model's
// (WorldLightEvaluator, WorldLightModel) at a pinned 06:00, a frame's second publish takes no second step, _WmvWLOn is 0 for
// every other camera's begin and after every camera's end -- through the handlers and through a real render of a camera that
// is not the main one --, pinned off for the fixtures, off for an invalid light and when the host switches it off, the Light
// -1 when the evaluation found none, the ease eased across a point light's ramp and snapped only when -wmvAnimTime pins the
// clocks; the owner rule: a second WmvMain's DisposeTerrain leaves the handlers installed, only the owner's Release removes
// them. Steps 4 and 5: WmvTerrainComposite's render really draws with _WmvWLOn 0 (a textured-terrain probe that is white
// unlit and lit otherwise: white inside the composite, the CPU twin's lit colour when the probe's own command buffer sets 1);
// the lit branch's other paths on the GPU against the CPU twin (MCCV not 127, a downward normal with a zero ground colour, a
// normal facing away from the light with a zero horizon colour, the flat arm); the sky's GPU against its CPU twin
// (SkyDome.Trace, 3e-3) in two views; the sky on Camera.main's world view: drawn behind an opaque floor, recorded before the
// water's grab, the depth at its pixels still the far plane and the grab's coverage 0 there, the floor's pixels unchanged;
// which cameras get it; its mesh and material gone with the scene (WmvWorldLight.Off); bloom held at 0 and put back exactly.
// Step 6 (OutdoorModelTests, cases T1-T15): the world-lit variants on the GPU against the CPU twin (ModelOutdoorLight) -- T1 the
// raw interpolated normal (H and the wrap normalised, the direct term raw) and the terrain probe's other paths, T2 the flat
// arms, T3 a tint written as the animator writes it, T4 unlit, T5 alpha-tested foliage, T6 the else-branch against the
// original (reported), T7 the original untouched by the light's globals, T8 two-sided, T9 the copy, T10 the diagnostic views;
// the binding -- T11 a world model's exterior and interior groups through the real WmvWorldModels (an instance placed while
// bound counted apart), T12 a session's MDDF and MODD doodads by their contexts (and the load's context line; doodads placed
// while bound), T13 _WmvWLModels, T14 ObjectsLit, the frame's binding and its driver's lines (the placements while shown,
// the first lit frame, the overview, a fault; why the light is not shown);
// T15 is in the liquids' tests. With WMV_LIGHT=off only the hard off is checked (nothing installed, nothing set, nothing
// said, no normal stream, no variant loaded and no world renderer bound). The driver's state is put back to the switches'
// start afterwards.

using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEngine;
using UnityEngine.Experimental.Rendering;
using UnityEngine.Rendering;
using Wmv.Wow;
using Wmv.Wow.World;

public static partial class WmvLifecycleSelfTest
{
    /// <summary>A loadWorld line carrying an outdoor light object for map 1: zone 139 (a square around the Southfury stand) ->
    /// Light 204 (LightParams 5985: its 06:00 and 12:00 keys of three colours and FogDensity), a two-point zone (malformed), the
    /// default Light 191 (LightParams 192 without keys), the point Light 300 (a white day) at (1300, -3948, 20) reaching 100 yd.
    /// `extra` goes in before "defaultFrom" (an optional key such as "timeOverride").</summary>
    static string WorldLightLine(string extra)
    {
        return "{\"type\":\"loadWorld\",\"load\":7,\"assetEpoch\":1,\"mapId\":1,\"wdtFileDataID\":782779,\"tileX\":40,\"tileY\":30," +
               "\"maxAssetBytes\":4194304,\"worldLight\":{\"state\":\"ok\",\"detail\":\"\",\"product\":\"wow\",\"build\":\"12.1.5.70448\",\"map\":1," +
               "\"branch\":\"retail\",\"lightDataLayout\":\"6ABF2921\",\"mapFlags\":[1,2,3],\"defaultLight\":191," + extra + "\"defaultFrom\":\"end0\"," +
               "\"zones\":[{\"zone\":139,\"light\":204,\"priority\":2,\"zmin\":-64000,\"zmax\":64000,\"conditional\":0," +
               "\"poly\":[1000,-4200,1500,-4200,1500,-3700,1000,-3700]},{\"zone\":5,\"light\":204,\"priority\":0,\"zmin\":0,\"zmax\":1," +
               "\"conditional\":0,\"poly\":[0,0,1,1]}]," +
               "\"points\":[{\"light\":300,\"x\":1300,\"y\":-3948,\"z\":20,\"start\":50,\"end\":100}]," +
               "\"lights\":[{\"light\":204,\"slots\":[5985,0,0,0,3,0,0,0]},{\"light\":191,\"slots\":[192,0,0,0,3,0,0,0]}," +
               "{\"light\":300,\"slots\":[301,0,0,0,0,0,0,0]}]," +
               "\"params\":[{\"id\":5985,\"day\":0,\"glow\":0,\"skybox\":0,\"flags\":0,\"highlightSky\":0,\"overrideSun\":[0,0,0],\"sunPolar\":0," +
               "\"sunAzimuth\":0},{\"id\":192,\"day\":-1,\"glow\":0.5,\"skybox\":0,\"flags\":0,\"highlightSky\":0,\"overrideSun\":[0,0,0]," +
               "\"sunPolar\":0,\"sunAzimuth\":0},{\"id\":301,\"day\":1,\"glow\":1,\"skybox\":7,\"flags\":512,\"highlightSky\":1," +
               "\"overrideSun\":[1,2,3],\"sunPolar\":90,\"sunAzimuth\":-90}]," +
               "\"colorColumns\":[\"DirectColor\",\"AmbientColor\",\"SkyMiddleColor\"],\"floatColumns\":[\"FogDensity\"]," +
               "\"intColumns\":[\"ColorGradingFileDataID\"],\"absentColumns\":[]," +
               "\"days\":[{\"times\":[720,1440],\"c\":[7885824,10001307,15576168,6173184,12035512,16769711],\"f\":[2.5,3.0],\"i\":[0,0]}," +
               "{\"times\":[0],\"c\":[16777215,16777215,16777215],\"f\":[9.0],\"i\":[77]}]}}";
    }

    /// <summary>A loadWorld line whose object carries the wire values the parser's float32 and long fields exist for: 0.15 and
    /// 0.05 (not exact in binary: read as the float the client holds), -0.15, flags and integers of 2^31 and more, map flags of
    /// 2^32 - 1; `timeOverride` a "timeOverride" key (with its comma) or "".</summary>
    static string WireValueLine(string timeOverride)
    {
        return "{\"type\":\"loadWorld\",\"load\":10,\"assetEpoch\":1,\"mapId\":1,\"worldLight\":{\"state\":\"ok\",\"map\":1," + timeOverride +
               "\"mapFlags\":[4294967295,2147483648,0],\"defaultLight\":5,\"defaultFrom\":\"origin\",\"zones\":[],\"points\":[]," +
               "\"lights\":[{\"light\":5,\"slots\":[50,0,0,0,0,0,0,0]}]," +
               "\"params\":[{\"id\":50,\"day\":0,\"glow\":0.15,\"skybox\":0,\"flags\":2147483648,\"highlightSky\":0,\"overrideSun\":[0.05,-0.15,1]," +
               "\"sunPolar\":0.05,\"sunAzimuth\":-0.15}]," +
               "\"colorColumns\":[\"DirectColor\"],\"floatColumns\":[\"FogScaler\",\"SunFogStrength\"]," +
               "\"intColumns\":[\"ColorGradingFileDataID\",\"DarkerColorGradingFileDataID\"],\"absentColumns\":[]," +
               "\"days\":[{\"times\":[0],\"c\":[16777215],\"f\":[0.15,0.05],\"i\":[2147483648,4294967296]}]}}";
    }

    static bool SameVector(Vector4 a, double x, double y, double z, double w)
    {
        return a.x == (float)x && a.y == (float)y && a.z == (float)z && a.w == (float)w;
    }

    static void OutdoorLightTests(Action<string> log)
    {
        OutdoorLightIpcTests(log);
        if (WmvWorldLight.HardOff)
        {
            OutdoorLightHardOffTests(log);
            return;
        }
        OutdoorLightDriverTests(log);
        OutdoorLightCompositeTests(log);
        OutdoorSkyFixtureTests(log);
        OutdoorSkyCameraTests(log);
        OutdoorBloomTests(log);
        OutdoorModelTests(log);         // step 6
    }

    static void OutdoorLightIpcTests(Action<string> log)
    {
        // ---- the offer, read as the reader thread reads it
        WorldLightSet s = WmvIpcClient.ParseWorldLight(WorldLightLine(""));
        WorldLightParams p301 = null;
        int[] slots204 = null;
        bool shape = s != null && s.State == "ok" && s.Map == 1 && s.Branch == "retail" && s.LightDataLayout == "6ABF2921" && s.DefaultLight == 191 &&
                     s.DefaultFrom == "end0" && s.MapFlags[2] == 3 && s.Zones.Count == 1 && s.DroppedZones == 1 && s.Zones[0].Priority == 2 &&
                     s.Points.Count == 1 && s.LightCount == 3 && s.ParamsCount == 3 && s.DayCount == 2 && s.DroppedDays == 0 && s.DroppedParams == 0 &&
                     s.TimeOverride == -1 && s.TryGetParams(301, out p301) && p301.Flags == 512 && p301.Skybox == 7 && p301.OverrideSun[2] == 3 &&
                     p301.SunAzimuth == -90 && s.TryGetSlots(204, out slots204) && slots204[4] == 3;
        int r = 0, g = 0, b = 0;
        if (shape)
            s.DayAt(0).Colour(1080, WorldLightColumns.SkyMiddleColor, false, out r, out g, out b);
        Check(shape && r == 246 && g == 199 && b == 140,
              "outdoor light ipc: \"worldLight\" read on a loadWorld line (one zone kept, the two-point one dropped and counted; a point light; " +
              "three lights with their 8 slots; three LightParams with days, flags, skybox and sun overrides; two days; no \"timeOverride\": none); " +
              "SkyMiddle at 09:00 half-way from (237,172,104) to (255,226,175) is (246,199,140) (" + (s != null ? s.Summary() : "nothing") + ")", log);
        WorldLightSet o = WmvIpcClient.ParseWorldLight(WorldLightLine("\"timeOverride\":360,"));
        WorldLightSet z = WmvIpcClient.ParseWorldLight(WorldLightLine("\"timeOverride\":0,"));
        WorldLightSet u = WmvIpcClient.ParseWorldLight("{\"type\":\"loadWorld\",\"load\":8,\"mapId\":1,\"worldLight\":{\"state\":\"unavailable\"," +
                                                       "\"detail\":\"test: no light tables\",\"map\":1,\"zones\":[{\"zone\":1}]}}");
        WorldLightSet none = WmvIpcClient.ParseWorldLight("{\"type\":\"loadWorld\",\"load\":9,\"mapId\":1}");
        Check(o != null && o.TimeOverride == 360 && o.TimeOverrideMinutes == 360 && z != null && z.TimeOverride == 0 && u != null &&
              u.State == "unavailable" && u.Detail == "test: no light tables" && u.Zones.Count == 0 && u.DayCount == 0 && none == null,
              "outdoor light ipc: \"timeOverride\" counts only when the line carries it (360, and 0 -- midnight -- read as such); \"unavailable\" kept " +
              "with why and nothing else read; a line without \"worldLight\" gives none", log);

        // ---- the wire values the float32 and long fields exist for, and a negative override
        WorldLightSet wv = WmvIpcClient.ParseWorldLight(WireValueLine(""));
        WorldLightSet minus1 = WmvIpcClient.ParseWorldLight(WireValueLine("\"timeOverride\":-1,"));
        WorldLightSet minus30 = WmvIpcClient.ParseWorldLight(WireValueLine("\"timeOverride\":-30,"));
        WorldLightParams p50 = null;
        WorldLightDay d50 = wv != null ? wv.DayAt(0) : null;
        bool floats = wv != null && wv.TryGetParams(50, out p50) && p50.Glow == (double)0.15f && p50.SunPolar == (double)0.05f &&
                      p50.SunAzimuth == (double)(-0.15f) && p50.OverrideSun[0] == (double)0.05f && p50.OverrideSun[1] == (double)(-0.15f) &&
                      p50.OverrideSun[2] == 1.0 && d50 != null &&
                      d50.Floats[WorldLightColumns.FogScaler] == (double)0.15f && d50.Floats[WorldLightColumns.SunFogStrength] == (double)0.05f &&
                      d50.Floats[WorldLightColumns.FogScaler] != 0.15;
        bool longs = p50 != null && p50.Flags == 2147483648L && d50 != null && d50.Ints[WorldLightColumns.ColorGradingFileDataID] == 2147483648L &&
                     d50.Ints[WorldLightColumns.DarkerColorGradingFileDataID] == 4294967296L && wv.MapFlags[0] == 4294967295L &&
                     wv.MapFlags[1] == 2147483648L;
        bool negative = minus1 != null && minus1.TimeOverride == -1 && minus1.TimeOverrideMinutes == -1 && minus30 != null &&
                        minus30.TimeOverride == -30 && minus30.TimeOverrideMinutes == -1 && wv != null && wv.TimeOverride == -1;
        Check(floats && longs && negative,
              "outdoor light ipc: the wire values -- 0.15, 0.05 and -0.15 read as the float32 the client holds ((double)0.15f, not 0.15) in Glow, " +
              "the sun overrides and a day's floats; flags 2^31 and integers 2^31 and 2^32 read whole; map flags 2^32 - 1; \"timeOverride\" -1 and " +
              "-30 both none (" + (d50 != null ? d50.Floats[WorldLightColumns.FogScaler].ToString("R", System.Globalization.CultureInfo.InvariantCulture) : "no day") +
              ")", log);

        // ---- through the client: the load command carries it, its log text still ends with the liquids; setWorldLight's fields
        var go = new GameObject("WorldLightIpcTest");
        try
        {
            var ipc = go.AddComponent<WmvIpcClient>();
            WmvIpcClient.TerrainTileCommand got = null;
            var commands = new List<WmvIpcClient.WorldLightCommand>();
            ipc.OnLoadTerrainTile = c => got = c;
            ipc.OnSetWorldLight = c => commands.Add(c);
            ipc.FeedLineForTest(WorldLightLine(""), null);
            // With WMV_LIGHT=off the command's text says nothing of the outdoor light (the log stays Stage 09's).
            bool text = WmvWorldLight.HardOff ? !got.ToString().Contains("outdoor light") : got.ToString().Contains("; outdoor light map 1 (wow 12.1.5.70448");
            bool carried = got != null && got.WorldLight != null && got.WorldLight.State == "ok" && got.WorldLight.Zones.Count == 1 && text &&
                           got.ToString().EndsWith("; no liquids offered", StringComparison.Ordinal);
            ipc.FeedLineForTest("{\"type\":\"setWorldLight\",\"on\":1,\"time\":\"06:00:00\"}", null);
            ipc.FeedLineForTest("{\"type\":\"setWorldLight\",\"clock\":\"running\"}", null);
            ipc.FeedLineForTest("{\"cmd\":\"setWorldLight\",\"on\":0}", null);
            WmvIpcClient.WorldLightCommand parsed;
            bool direct = WmvIpcClient.ParseSetWorldLight("{\"type\":\"setWorldLight\",\"clock\":\"pinned\",\"time\":\"18:30\"}", out parsed) &&
                          !parsed.HasOn && parsed.HasClock && parsed.Clock == "pinned" && parsed.HasTime && parsed.Time == "18:30" && !parsed.HasFarclip;
            direct &= WmvIpcClient.ParseSetWorldLight("{\"type\":\"setWorldLight\",\"farclip\":1500}", out parsed) && parsed.HasFarclip &&
                      parsed.Farclip == 1500 && !parsed.HasOn && !parsed.HasClock && !parsed.HasTime &&
                      !WmvIpcClient.ParseSetWorldLight("{\"type\":\"setTerrainView\",\"view\":\"auto\"}", out parsed);
            bool three = commands.Count == 3 && commands[0].HasOn && commands[0].On && commands[0].HasTime && commands[0].Time == "06:00:00" &&
                         !commands[0].HasClock && !commands[1].HasOn && commands[1].HasClock && commands[1].Clock == "running" && !commands[1].HasTime &&
                         commands[2].HasOn && !commands[2].On && !commands[2].HasClock && !commands[2].HasTime;
            Check(carried && three && direct,
                  "outdoor light ipc: the load command carries the object (its log text names it before the liquids -- nothing of it with " +
                  "WMV_LIGHT=off -- and still ends \"; no liquids offered\"); setWorldLight's fields are present only when the line carries them " +
                  "(on 1 and a time; a clock alone; on 0 -- by \"cmd\", the spec's spelling -- read as off, not absent)", log);

            // ---- runtimeState: the light fields before the liquid counters (the line still ends with them); -1 without a state
            var sent = new List<string>();
            ipc.SentForTest = x => sent.Add(x);
            ipc.ReportRuntimeState(12, new WmvIpcClient.RuntimeReport
            {
                MountKey = "", LightFilled = true, LightOn = true, LightTime = "06:00:00", LightTimeSource = "pinned", LightZone = 139, LightId = 204,
                LightParams = 5985, LightValid = true, GameFarclip = 7000, SkyOn = true, LiquidObjects = 1, LiquidGpuBytes = 5,
            });
            ipc.ReportRuntimeState(13, new WmvIpcClient.RuntimeReport { MountKey = "" });
            string filled = sent.Count == 2 ? sent[0] : "", empty = sent.Count == 2 ? sent[1] : "";
            Check(filled.Contains(",\"lightOn\":1,\"lightTime\":\"06:00:00\",\"lightTimeSource\":\"pinned\",\"lightZone\":139,\"lightId\":204," +
                                  "\"lightParams\":5985,\"lightValid\":1,\"gameFarclip\":7000,\"skyOn\":1,\"liquidObjects\":1,") &&
                  filled.EndsWith(",\"liquidGpuBytes\":5}", StringComparison.Ordinal) &&
                  empty.Contains(",\"lightOn\":0,\"lightTime\":\"\",\"lightTimeSource\":\"\",\"lightZone\":-1,\"lightId\":-1,\"lightParams\":-1," +
                                 "\"lightValid\":0,\"gameFarclip\":-1,\"skyOn\":0,"),
                  "outdoor light ipc: runtimeState carries lightOn, lightTime, lightTimeSource, lightZone, lightId, lightParams, lightValid, gameFarclip " +
                  "and skyOn before the liquid counters (the line still ends with them); a report without the light state sends -1", log);
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(go);
        }
    }

    /// <summary>WMV_LIGHT=off: Configure installs nothing and says nothing, Frame, Sky and setWorldLight set nothing, runtimeState
    /// says off.</summary>
    static void OutdoorLightHardOffTests(Action<string> log)
    {
        object standIn = new object();
        var said = new List<string>();
        Action<string> savedLog = WmvWorldLight.Log;
        WmvWorldLight.Configure(standIn, x => said.Add(x));
        int saidAtConfigure = said.Count;
        WorldLightSet s = WmvIpcClient.ParseWorldLight(WorldLightLine(""));
        WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasOn = true, On = true });
        WmvWorldLight.Frame(-3000, 0.016, s, true, 1225, -3948, 20, 0, false);
        WmvWorldLight.Sky(Camera.main, true);
        var r = new WmvIpcClient.RuntimeReport();
        WmvWorldLight.Fill(ref r);
        Check(!WmvWorldLight.Installed && WmvWorldLight.Owner == null && !WmvWorldLight.Enabled && !WmvWorldLight.PublishedOn && !WmvWorldLight.Evaluated &&
              !r.LightOn && r.LightId == -1 && !r.SkyOn && WmvWorldLight.Describe() == "world light off (WMV_LIGHT=off)" && !WmvWorldLight.Release(standIn) &&
              saidAtConfigure == 0 && !WmvWorldSky.BloomHeld && WmvWorldSky.MaterialCount == 0 && !WmvWorldLight.TerrainNormalsWanted &&
              !WmvWorldLight.TerrainNormalsUploaded,
              "outdoor light: WMV_LIGHT=off -- nothing installed and no line said at Configure, the host's \"on\" refused, nothing evaluated, set, " +
              "drawn or held (no sky, bloom untouched, no terrain normal stream); runtimeState off", log);
        try { OutdoorModelHardOffTests(log, said); }
        catch (Exception e) { Check(false, "outdoor models: the hard-off test threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log); }
        WmvWorldLight.Log = savedLog;
    }

    static void OutdoorLightDriverTests(Action<string> log)
    {
        bool wasInstalled = WmvWorldLight.Installed;
        object wasOwner = WmvWorldLight.Owner;
        object standIn = new object();
        var objects = new List<GameObject>();
        RenderTexture target = null;
        Action<ScriptableRenderContext, Camera> beginProbe = null, endProbe = null;
        try
        {
            // ---- the owner: the first Configure installs and owns, a later one does not take it over
            if (!wasInstalled)
            {
                WmvWorldLight.Configure(standIn, null);
                WmvWorldLight.Configure(new object(), null);
                Check(WmvWorldLight.Installed && WmvWorldLight.Owner == standIn,
                      "outdoor light: Configure installs the per-camera handlers once; the first caller owns them, a later one does not", log);
            }
            object owner = WmvWorldLight.Owner;

            // ---- on, pinned at 06:00: the globals equal the pure model's
            WorldLightSet s = WmvIpcClient.ParseWorldLight(WorldLightLine(""));
            WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasOn = true, On = true, HasClock = true, Clock = "pinned", HasTime = true,
                                                                           Time = "06:00:00" });
            const int F = -4000;                                   // frame numbers Time.frameCount never takes
            double x = 1225, y = -3948, zz = 20;                   // inside zone 139, 75 yd from the point light: weight 0.5
            WmvWorldLight.Frame(F, 0.016, s, true, x, y, zz, 17.5, false);
            var ev = new WorldLightEvaluator();
            var want = new WorldLightSample();
            ev.Evaluate(s, x, y, zz, 6 * 3600.0, 0, WmvWorldLight.Switches.Rules, want);
            double lx, ly, lz, sx, sy, sz;
            WorldLightModel.LightDirection(6 * 3600.0, out lx, out ly, out lz);
            WorldLightModel.SunDirection(6 * 3600.0, out sx, out sy, out sz);
            Func<int, int, bool> colour = (id, column) =>
            {
                int at = WorldLightSample.ColourAt(column);
                return SameVector(Shader.GetGlobalVector(id), want.V[at], want.V[at + 1], want.V[at + 2], 1.0);
            };
            Vector4 frame = Shader.GetGlobalVector(WmvWorldLight.FrameId), fogA = Shader.GetGlobalVector(WmvWorldLight.FogAId);
            Vector4 terrainGlobal = Shader.GetGlobalVector(WmvWorldLight.TerrainId);
            bool equal = WmvWorldLight.PublishedOn && Shader.GetGlobalFloat(WmvWorldLight.OnId) == 1f && want.Valid && want.Points == 1 &&
                         colour(WmvWorldLight.DirectId, WorldLightColumns.DirectColor) && colour(WmvWorldLight.AmbientId, WorldLightColumns.AmbientColor) &&
                         colour(WmvWorldLight.SkyMiddleId, WorldLightColumns.SkyMiddleColor) && colour(WmvWorldLight.SkyTopId, WorldLightColumns.SkyTopColor) &&
                         fogA.z == (float)want.Float(WorldLightColumns.FogDensity) && fogA.z == 5.75f &&
                         Shader.GetGlobalVector(WmvWorldLight.LightDirId) == WmvWorldLight.Direction(new[] { lx, ly, lz }) &&
                         Shader.GetGlobalVector(WmvWorldLight.SunDirId) == WmvWorldLight.Direction(new[] { sx, sy, sz }) &&
                         frame.x == (float)WmvWorldLight.FarClip && frame.y == 17.5f && frame.z == 720f && frame.w == 21600f &&
                         terrainGlobal.x == (WmvWorldLight.Switches.TerrainLight ? 1f : 0f) && terrainGlobal.y == (WmvWorldLight.Switches.TerrainFlat ? 1f : 0f);
            var rs = new WmvIpcClient.RuntimeReport();
            WmvWorldLight.Fill(ref rs);
            bool state = rs.LightOn && rs.LightValid && rs.LightZone == 139 && rs.LightId == 204 && rs.LightParams == 5985 && rs.LightTime == "06:00:00" &&
                         rs.LightTimeSource == "pinned" && rs.GameFarclip == WmvWorldLight.FarClip;
            bool stream = WmvWorldLight.TerrainNormalsUploaded == WmvWorldLight.Switches.TerrainLight &&
                          WmvWorldLight.TerrainNormalsWanted == WmvWorldLight.Switches.TerrainLight;
            Check(equal && state && stream,
                  "outdoor light: on, pinned at 06:00, inside zone 139 with the white point light at weight 0.5 -- every published global equals the " +
                  "pure model's (Direct, Ambient and SkyMiddle half-way to white, SkyTop -- not sent -- 0; FogDensity (2.5 + 9) / 2 = 5.75; the light " +
                  "direction and the sun from the graded tables; the far clip, origin Z 17.5, half-minute 720, 21600 s; _WmvWLTerrain from the " +
                  "switches); runtimeState: zone 139, Light 204, LightParams 5985, valid; a tile built now gets the terrain's normal stream " +
                  "(unless -wmvTerrainLight=off) (" + WmvWorldLight.Describe() + ")", log);

            // ---- the ease (review finding 2): across the point light's ramp it eases with the world clock pinned, and snaps only
            // when -wmvAnimTime pins the clocks.
            WmvWorldLight.Frame(F + 10, 0.016, s, true, x - 10, y, zz, 17.5, false);     // 85 yd from the point light: weight 0.3
            int sm = WorldLightSample.ColourAt(WorldLightColumns.SkyMiddleColor) + 1;
            double target0 = WmvWorldLight.Sample.V[sm], eased = WmvWorldLight.Ease.V[sm];
            bool easesPinnedClock = WmvWorldLight.Clock.Source == WorldClock.SourcePinned && Math.Abs(eased - target0) > 1e-6;
            WmvWorldLight.Frame(F + 11, 0.016, s, true, x - 20, y, zz, 17.5, true);      // 95 yd: weight 0.1, -wmvAnimTime
            bool snapsAnimPinned = WmvWorldLight.Ease.V[sm] == WmvWorldLight.Sample.V[sm];
            Check(easesPinnedClock && snapsAnimPinned,
                  "outdoor light: the ease across a point light's ramp eases while the world clock is pinned (" + eased.ToString("0.####") + " towards " +
                  target0.ToString("0.####") + ") and snaps when -wmvAnimTime pins the clocks, as the water's zone light does", log);
            WmvWorldLight.Frame(F + 12, 0.016, s, true, x, y, zz, 17.5, true);           // back at the start, snapped

            // ---- once per frame; every other camera starts at 0, every camera ends at 0
            Camera main = Camera.main;
            var camGo = new GameObject("WorldLightTestCamera");
            objects.Add(camGo);
            Camera other = camGo.AddComponent<Camera>();
            other.enabled = false;
            bool otherIsMain = other == main;
            WmvWorldLight.BeginCameraForTest(other);
            bool otherBegin = !WmvWorldLight.PublishedOn && Shader.GetGlobalFloat(WmvWorldLight.OnId) == 0f;
            WmvWorldLight.Frame(F + 12, 0.016, null, false, 0, 0, 0, 0, false);    // the same frame again: no second step, the state published again
            bool again = WmvWorldLight.PublishedOn && WmvWorldLight.Evaluated && WmvWorldLight.Sample.Light == 204;
            WmvWorldLight.EndCameraForTest(main != null ? main : other);
            bool end = !WmvWorldLight.PublishedOn && Shader.GetGlobalFloat(WmvWorldLight.OnId) == 0f;
            if (main != null)
            {
                WmvWorldLight.Frame(F + 12, 0.016, null, false, 0, 0, 0, 0, false);
                WmvWorldLight.BeginCameraForTest(main);
            }
            bool mainKept = main == null || WmvWorldLight.PublishedOn;
            Check(!otherIsMain && otherBegin && again && end && mainKept,
                  "outdoor light: another camera's begin sets _WmvWLOn 0; a second call in the same frame takes no second step and publishes again; " +
                  "every camera's end sets 0; the main camera's begin leaves its state", log);

            // A real render of the camera that is not the main one: the probes, run after the light's handlers, see 0 in it and after it.
            float seenBegin = -1f, seenEnd = -1f;
            beginProbe = (ctx, c) => { if (c == other) seenBegin = Shader.GetGlobalFloat(WmvWorldLight.OnId); };
            endProbe = (ctx, c) => { if (c == other) seenEnd = Shader.GetGlobalFloat(WmvWorldLight.OnId); };
            RenderPipelineManager.beginCameraRendering += beginProbe;
            RenderPipelineManager.endCameraRendering += endProbe;
            target = new RenderTexture(8, 8, 16, RenderTextureFormat.ARGB32) { name = "WmvWorldLight test target" };
            target.Create();
            other.targetTexture = target;
            other.cullingMask = 0;
            other.clearFlags = CameraClearFlags.SolidColor;
            WmvWorldLight.Frame(F + 12, 0.016, null, false, 0, 0, 0, 0, false);    // published: 1 before the render
            bool before = Shader.GetGlobalFloat(WmvWorldLight.OnId) == 1f;
            other.Render();
            Check(before && seenBegin == 0f && seenEnd == 0f && Shader.GetGlobalFloat(WmvWorldLight.OnId) == 0f,
                  "outdoor light: a real render of a camera that is not the main one sees _WmvWLOn 0 at its begin and its end (" + seenBegin + ", " +
                  seenEnd + "; 1 before it)", log);

            // ---- pinned off for the fixtures; the composite's renders put it back as they found it
            WmvWorldLight.PinOff();
            WmvWorldLight.Frame(F + 12, 0.016, null, false, 0, 0, 0, 0, false);
            bool pinned = !WmvWorldLight.PublishedOn && WmvWorldLight.PinnedOff;
            WmvWorldLight.Unpin();
            WmvWorldLight.Frame(F + 12, 0.016, null, false, 0, 0, 0, 0, false);
            bool unpinned = WmvWorldLight.PublishedOn;
            WmvTerrainComposite.RenderToArray(cmd => { }, 0f, 8f, 8f, 8f, 0f, 10f, 2, 2, false, false);
            bool restored = Shader.GetGlobalFloat(WmvWorldLight.OnId) == 1f;
            Check(pinned && unpinned && restored,
                  "outdoor light: pinned off for a fixture (nested PinOff/Unpin), on again after; WmvTerrainComposite's render puts 1 back (that it " +
                  "draws with 0 is the composite check below)", log);

            // ---- invalid, no light found, no object, switched off by the host, the time override
            WmvWorldLight.Frame(F + 1, 0.016, s, true, 0, 0, 0, 0, false);    // outside the zone: the default Light 191 has no keys
            var ri = new WmvIpcClient.RuntimeReport();
            WmvWorldLight.Fill(ref ri);
            bool invalid = !WmvWorldLight.PublishedOn && !ri.LightValid && ri.LightZone == 0 && ri.LightId == 191 && ri.LightParams == -1;
            var empty = new WorldLightSet { State = "ok", Map = 1 };      // no zone, no default light: the evaluation finds none
            WmvWorldLight.Frame(F + 5, 0.016, empty, true, x, y, zz, 0, false);
            var rf = new WmvIpcClient.RuntimeReport();
            WmvWorldLight.Fill(ref rf);
            bool noneFound = WmvWorldLight.Evaluated && !rf.LightValid && rf.LightZone == -1 && rf.LightId == -1 && rf.LightParams == -1;
            WmvWorldLight.Frame(F + 2, 0.016, null, false, 0, 0, 0, 0, false);
            var rn = new WmvIpcClient.RuntimeReport();
            WmvWorldLight.Fill(ref rn);
            bool noObject = !WmvWorldLight.PublishedOn && rn.LightOn && rn.LightZone == -1 && rn.LightId == -1;
            WorldLightSet o = WmvIpcClient.ParseWorldLight(WorldLightLine("\"timeOverride\":1080,"));
            WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasFarclip = true, Farclip = 1500 });
            WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasFarclip = true, Farclip = -5 });
            WmvWorldLight.Frame(F + 3, 0.016, o, true, x, y, zz, 0, false);
            bool map = WmvWorldLight.Clock.Source == WorldClock.SourceMap && WmvWorldLight.Sample.HalfMinute == 2160 && WmvWorldLight.PublishedOn &&
                       WmvWorldLight.FarClip == 1500 && Shader.GetGlobalVector(WmvWorldLight.FrameId).x == 1500f;
            WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasOn = true, On = false });
            WmvWorldLight.Frame(F + 4, 0.016, o, true, x, y, zz, 0, false);
            var ro = new WmvIpcClient.RuntimeReport();
            WmvWorldLight.Fill(ref ro);
            bool off = !WmvWorldLight.PublishedOn && !ro.LightOn && ro.LightId == -1 && !ro.SkyOn && WmvWorldLight.Describe() == "world light off" &&
                       !WmvWorldLight.TerrainNormalsUploaded && WmvWorldLight.TerrainNormalsWanted == WmvWorldLight.Switches.TerrainLight;
            Check(invalid && noneFound && noObject && map && off,
                  "outdoor light: 0 for an invalid light (the default Light 191 without keys: runtimeState zone 0, Light 191, LightParams -1); an " +
                  "evaluation that finds no light reads Light -1 (not 0); 0 with no object (-1); the map's override 1080 min takes the light to 18:00 " +
                  "(\"map\"), the host's far clip 1500 published (-5 refused), and 0 once the host switches it off -- a tile built then gets no " +
                  "normal stream (Stage 09's vertex data) but keeps its MCNR bytes for a later switch on", log);

            // ---- the owner rule: a second WmvMain's DisposeTerrain leaves the handlers alone
            var mainGo = new GameObject("WorldLightSecondMain");
            objects.Add(mainGo);
            mainGo.SetActive(false);                                // before AddComponent: no Awake
            WmvMain second = mainGo.AddComponent<WmvMain>();
            InvokeMain(second, "DisposeTerrain");
            bool kept = WmvWorldLight.Installed && WmvWorldLight.Owner == owner;
            bool stranger = !WmvWorldLight.Release(second) && !WmvWorldLight.Release(null) && WmvWorldLight.Installed;
            bool released = true;
            if (!wasInstalled)
                released = WmvWorldLight.Release(standIn) && !WmvWorldLight.Installed && WmvWorldLight.Owner == null && !WmvWorldLight.Release(standIn);
            Check(kept && stranger && released,
                  "outdoor light: a second WmvMain's DisposeTerrain (and any other caller's Release) leaves the handlers installed; only the owner's " +
                  "Release removes them", log);
        }
        catch (Exception e)
        {
            Exception cause = e is TargetInvocationException && e.InnerException != null ? e.InnerException : e;
            Check(false, "outdoor light: the test threw " + cause.GetType().Name + ": " + cause.Message + " at " + cause.StackTrace, log);
        }
        finally
        {
            if (beginProbe != null)
                RenderPipelineManager.beginCameraRendering -= beginProbe;
            if (endProbe != null)
                RenderPipelineManager.endCameraRendering -= endProbe;
            foreach (GameObject go in objects)
                if (go != null)
                {
                    Camera c = go.GetComponent<Camera>();
                    if (c != null)
                        c.targetTexture = null;
                    UnityEngine.Object.DestroyImmediate(go);
                }
            if (target != null)
            {
                target.Release();
                UnityEngine.Object.DestroyImmediate(target);
            }
            if (!wasInstalled && WmvWorldLight.Owner == standIn)
                WmvWorldLight.Release(standIn);
            // Back to the switches' start (the player's own Configure follows this self-test).
            WmvWorldLight.ApplySwitches();
            WmvWorldLight.Off();
        }
        Check(WmvWorldLight.Installed == wasInstalled && WmvWorldLight.Owner == wasOwner && !WmvWorldLight.PublishedOn,
              "outdoor light: the driver is as the test found it (installed " + wasInstalled + ", the switches' start state, _WmvWLOn 0)", log);
    }

    // ---------------------------------------------------------------- step 5: the composite draws unlit; the lit branch's GPU

    /// <summary>The composite's renders draw with _WmvWLOn 0 (review finding 10), proved by a probe that is white unlit and
    /// lit otherwise: a Wmv/TerrainTextured material of one white layer (no MCCV) on a quad with a tilted, unnormalised light
    /// normal in UV1. Inside WmvTerrainComposite.RenderToArray -- while the light's globals say on and lit -- every pixel is
    /// exactly 1 (the unlit composite); when the probe's own command buffer sets _WmvWLOn 1 after the composite's pin, every
    /// pixel is the CPU twin's lit colour (TerrainOutdoorLight.Shade, the normal at its own length) within 2e-3 (half floats).
    /// Then the branch's other paths, each against the twin (review finding 8): MCCV bytes not 127 (the albedo's x 254/255), a
    /// downward normal with a zero ground colour, a normal facing away from the light with a zero horizon colour, and the flat
    /// arm. The globals are put back.</summary>
    static void OutdoorLightCompositeTests(Action<string> log)
    {
        Shader textured = WmvTerrainMaterials.ResolveShader(null);
        if (textured == null)
        {
            log("lifecycle-test SKIP: outdoor light composite: Resources/" + WmvTerrainMaterials.ShaderResource + ".shader is missing");
            return;
        }
        var made = new List<UnityEngine.Object>();
        int[] ids = { WmvWorldLight.OnId, WmvWorldLight.TerrainId, WmvWorldLight.LightDirId, WmvWorldLight.AmbientId, WmvWorldLight.HorizonAmbientId,
                      WmvWorldLight.GroundAmbientId, WmvWorldLight.DirectId };
        var saved = new Vector4[ids.Length];
        for (int k = 0; k < ids.Length; k++)
            saved[k] = Shader.GetGlobalVector(ids[k]);
        float savedOn = Shader.GetGlobalFloat(WmvWorldLight.OnId);
        try
        {
            var m = new Material(textured) { name = "WmvWorldLight composite probe" };
            made.Add(m);
            m.SetTexture("_WmvLayer0", Texture2D.whiteTexture);
            m.SetFloat("_WmvLayerCount", 1f);
            m.SetVector("_WmvRepeats", new Vector4(8f, 8f, 8f, 8f));
            m.SetFloat("_WmvTexturedView", 0f);
            m.SetFloat("_WmvVertexColors", 0f);
            m.SetFloat("_WmvLinearOut", 0f);
            m.SetFloat("_Cull", (float)CullMode.Off);
            var quad = new Mesh { name = "WmvWorldLight composite probe quad" };
            made.Add(quad);
            var n = new Vector3(0.2f, 0.9f, -0.1f);                 // the light normal: tilted, not unit length
            quad.SetVertices(new List<Vector3> { new Vector3(0f, 50f, 0f), new Vector3(8f, 50f, 0f), new Vector3(8f, 50f, -8f), new Vector3(0f, 50f, -8f) });
            quad.SetNormals(new List<Vector3> { Vector3.up, Vector3.up, Vector3.up, Vector3.up });
            quad.SetUVs(0, new List<Vector2> { new Vector2(0f, 0f), new Vector2(8f, 0f), new Vector2(8f, 8f), new Vector2(0f, 8f) });
            quad.SetUVs(1, new List<Vector3> { n, n, n, n });
            quad.SetIndices(new[] { 0, 1, 2, 0, 2, 3 }, MeshTopology.Triangles, 0);
            quad.RecalculateBounds();

            var ambient = new Vector4(0.25f, 0.30f, 0.40f, 1f);
            var horizon = new Vector4(0.50f, 0.20f, 0.10f, 1f);
            var ground = new Vector4(0.05f, 0.10f, 0.15f, 1f);
            var direct = new Vector4(0.60f, 0.30f, 0.0f, 1f);
            Vector3 l = new Vector3(0.5f, 0.6f, 0.3f).normalized;
            var lightDir = new Vector4(l.x, l.y, l.z, 0f);
            // The light published "on and lit" for the main camera, as WmvWorldLight leaves the globals.
            Shader.SetGlobalFloat(WmvWorldLight.OnId, 1f);
            Shader.SetGlobalVector(WmvWorldLight.TerrainId, new Vector4(1f, 0f, 0f, 0f));
            Shader.SetGlobalVector(WmvWorldLight.LightDirId, lightDir);
            Shader.SetGlobalVector(WmvWorldLight.AmbientId, ambient);
            Shader.SetGlobalVector(WmvWorldLight.HorizonAmbientId, horizon);
            Shader.SetGlobalVector(WmvWorldLight.GroundAmbientId, ground);
            Shader.SetGlobalVector(WmvWorldLight.DirectId, direct);

            float[] pinned = WmvTerrainComposite.RenderToArray(cmd => cmd.DrawMesh(quad, Matrix4x4.identity, m, 0, 0), 0f, 0f, 8f, 8f, 40f, 60f, 4, 4,
                                                                false, false);
            float[] lit = WmvTerrainComposite.RenderToArray(cmd =>
            {
                cmd.SetGlobalFloat(WmvWorldLight.OnId, 1f);
                cmd.DrawMesh(quad, Matrix4x4.identity, m, 0, 0);
            }, 0f, 0f, 8f, 8f, 40f, 60f, 4, 4, false, false);
            var want = new double[3];
            TerrainOutdoorLight.Shade(new[] { 1.0, 1.0, 1.0 }, new double[] { n.x, n.y, n.z }, new double[] { l.x, l.y, l.z },
                                      new double[] { ambient.x, ambient.y, ambient.z }, new double[] { horizon.x, horizon.y, horizon.z },
                                      new double[] { ground.x, ground.y, ground.z }, new double[] { direct.x, direct.y, direct.z }, false, want);
            int white = 0;
            double worst = 0.0;
            for (int i = 0; i < 16; i++)
            {
                if (pinned[4 * i] == 1f && pinned[4 * i + 1] == 1f && pinned[4 * i + 2] == 1f)
                    white++;
                for (int c = 0; c < 3; c++)
                    worst = Math.Max(worst, Math.Abs(lit[4 * i + c] - want[c]));
            }
            bool back = Shader.GetGlobalFloat(WmvWorldLight.OnId) == 1f;
            Check(white == 16 && worst <= 2e-3 && want[0] < 0.9 && back,
                  string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                "outdoor light composite: inside WmvTerrainComposite's render the textured terrain draws UNLIT although the light's " +
                                "globals say on (all {0} of 16 pixels exactly white: _WmvWLOn is 0 during the render), and lit when the probe's own " +
                                "command buffer sets 1 -- the hemisphere branch against the CPU twin (TerrainOutdoorLight.Shade: ({1:0.####}, " +
                                "{2:0.####}, {3:0.####})), worst {4:0.######} (2e-3: half floats); _WmvWLOn put back to 1 after", white, want[0], want[1],
                                want[2], worst), log);

            // ---- the branch's other paths (review finding 8), each lit by the probe's own command buffer and against the CPU twin.
            var mccvMaterial = new Material(m) { name = "WmvWorldLight composite probe (MCCV)" };
            made.Add(mccvMaterial);
            mccvMaterial.SetFloat("_WmvVertexColors", 1f);
            var black = new Vector4(0f, 0f, 0f, 1f);
            Vector3 l2 = new Vector3(0.2f, -0.4f, 0.9f).normalized, l3 = new Vector3(-0.4f, 0.3f, 0.8f).normalized;
            Vector3 down = new Vector3(0.1f, -0.7f, 0.3f), away = new Vector3(0.3f, 0.6f, -0.5f);
            // MCCV bytes (60, 200, 150), not 127: the mesh holds byte / 127, the branch's x 254/255 gives the client's 2 x byte / 255
            // (an albedo above 1 in green, lit as it is).
            byte[] mccv = { 60, 200, 150 };
            var wMccv = new double[3];
            double eMccv = LightProbeWorst(mccvMaterial, LightProbeQuad(n, mccv, made), false, ClientAlbedo(mccv), n, l, ambient, horizon, ground,
                                           direct, wMccv);
            // A downward normal (N.U -0.7) with a zero ground colour: the ground lerp falls back to Ambient (lit from below: N.L > 0).
            var wDown = new double[3];
            double eDown = LightProbeWorst(m, LightProbeQuad(down, null, made), false, new[] { 1.0, 1.0, 1.0 }, down, l2, ambient, horizon, black,
                                           direct, wDown);
            // A normal facing away from the light (N.L < 0) with a zero horizon colour: no direct light (max(N.L, 0)), the wrap
            // below 0.9 (N.L not saturated), H = Ambient.
            var wAway = new double[3];
            double eAway = LightProbeWorst(m, LightProbeQuad(away, null, made), false, new[] { 1.0, 1.0, 1.0 }, away, l3, ambient, black, ground,
                                           direct, wAway);
            double nlAway = away.x * l3.x + away.y * l3.y + away.z * l3.z, nlDown = down.x * l2.x + down.y * l2.y + down.z * l2.z;
            // The flat arm (_WmvWLTerrain.y): albedo x sat(Direct x max(N.L, 0) + Ambient) x 1, its saturate taken on red (albedo
            // 0.63 from MCCV 80).
            byte[] mccvFlat = { 80, 100, 120 };
            var ambientFlat = new Vector4(0.45f, 0.30f, 0.40f, 1f);
            var directFlat = new Vector4(1.0f, 0.30f, 0.0f, 1f);
            var wFlat = new double[3];
            double eFlat = LightProbeWorst(mccvMaterial, LightProbeQuad(n, mccvFlat, made), true, ClientAlbedo(mccvFlat), n, l, ambientFlat, horizon,
                                           ground, directFlat, wFlat);
            double nl = n.x * l.x + n.y * l.y + n.z * l.z;
            bool flatSaturates = directFlat.x * nl + ambientFlat.x > 1.0;
            bool inRange = true;
            foreach (double[] w in new[] { wMccv, wDown, wAway, wFlat })
                for (int c = 0; c < 3; c++)
                    inRange &= w[c] > 0.02 && w[c] < 0.97;              // never at the output's saturate
            Check(eMccv <= 2e-3 && eDown <= 2e-3 && eAway <= 2e-3 && eFlat <= 2e-3 && inRange && nlDown > 0.0 && nlAway < 0.0 && flatSaturates &&
                  Shader.GetGlobalFloat(WmvWorldLight.OnId) == 1f,
                  string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                "outdoor light composite: the lit branch's other paths on the GPU against the CPU twin, worst channel each (2e-3) -- " +
                                "MCCV (60, 200, 150) not 127 ({0:0.######}: ({4:0.###}, {5:0.###}, {6:0.###}), the albedo 2 x byte / 255 through the " +
                                "mesh's byte / 127 x 254/255, green's above 1 not saturated); a downward normal with a zero ground colour ({1:0.######}: " +
                                "({7:0.###}, {8:0.###}, {9:0.###}), the ground lerp on Ambient); a normal facing away from the light, N.L {12:0.###}, " +
                                "with a zero horizon colour ({2:0.######}: ({10:0.###}, {11:0.###}, ...), no direct light, the wrap below 0.9); the flat " +
                                "arm ({3:0.######}, its saturate taken on red)", eMccv, eDown, eAway, eFlat, wMccv[0], wMccv[1], wMccv[2], wDown[0],
                                wDown[1], wDown[2], wAway[0], wAway[1], nlAway), log);
        }
        catch (Exception e)
        {
            Check(false, "outdoor light composite: the test threw " + e.GetType().Name + ": " + e.Message, log);
        }
        finally
        {
            for (int k = 0; k < ids.Length; k++)
                Shader.SetGlobalVector(ids[k], saved[k]);
            Shader.SetGlobalFloat(WmvWorldLight.OnId, savedOn);
            foreach (UnityEngine.Object x in made)
                if (x != null)
                    UnityEngine.Object.DestroyImmediate(x);
        }
    }

    /// <summary>A probe quad over the composite's 8 x 8 yd window at height 50: the light normal `n` in UV1 at every corner and,
    /// with `mccv` (R, G, B bytes), the builder's MCCV colour (byte / 127) at every corner.</summary>
    static Mesh LightProbeQuad(Vector3 n, byte[] mccv, List<UnityEngine.Object> made)
    {
        var quad = new Mesh { name = "WmvWorldLight probe quad" };
        made.Add(quad);
        quad.SetVertices(new List<Vector3> { new Vector3(0f, 50f, 0f), new Vector3(8f, 50f, 0f), new Vector3(8f, 50f, -8f), new Vector3(0f, 50f, -8f) });
        quad.SetNormals(new List<Vector3> { Vector3.up, Vector3.up, Vector3.up, Vector3.up });
        quad.SetUVs(0, new List<Vector2> { new Vector2(0f, 0f), new Vector2(8f, 0f), new Vector2(8f, 8f), new Vector2(0f, 8f) });
        quad.SetUVs(1, new List<Vector3> { n, n, n, n });
        if (mccv != null)
        {
            var c = new Color(mccv[0] / 127f, mccv[1] / 127f, mccv[2] / 127f, 1f);
            quad.SetColors(new List<Color> { c, c, c, c });
        }
        quad.SetIndices(new[] { 0, 1, 2, 0, 2, 3 }, MeshTopology.Triangles, 0);
        quad.RecalculateBounds();
        return quad;
    }

    /// <summary>The client's albedo of a white composite under MCCV bytes (R, G, B): 2 x byte / 255.</summary>
    static double[] ClientAlbedo(byte[] mccv)
    {
        var a = new double[3];
        for (int c = 0; c < 3; c++)
            a[c] = TerrainOutdoorLight.VertexColourFactor * mccv[c] / 255.0;
        return a;
    }

    /// <summary>One lit probe: `quad` drawn with `m` inside WmvTerrainComposite's render, the probe's own command buffer setting
    /// the light on (the flat arm with `flat`) and these colours and direction after the composite's pin; returns the 16
    /// pixels' worst channel against the CPU twin (TerrainOutdoorLight.Shade on `albedo`), whose colour goes in `want`. The
    /// caller puts the globals back.</summary>
    static double LightProbeWorst(Material m, Mesh quad, bool flat, double[] albedo, Vector3 n, Vector3 l, Vector4 ambient, Vector4 horizon,
                                  Vector4 ground, Vector4 direct, double[] want)
    {
        float[] lit = WmvTerrainComposite.RenderToArray(cmd =>
        {
            cmd.SetGlobalFloat(WmvWorldLight.OnId, 1f);
            cmd.SetGlobalVector(WmvWorldLight.TerrainId, new Vector4(1f, flat ? 1f : 0f, 0f, 0f));
            cmd.SetGlobalVector(WmvWorldLight.LightDirId, new Vector4(l.x, l.y, l.z, 0f));
            cmd.SetGlobalVector(WmvWorldLight.AmbientId, ambient);
            cmd.SetGlobalVector(WmvWorldLight.HorizonAmbientId, horizon);
            cmd.SetGlobalVector(WmvWorldLight.GroundAmbientId, ground);
            cmd.SetGlobalVector(WmvWorldLight.DirectId, direct);
            cmd.DrawMesh(quad, Matrix4x4.identity, m, 0, 0);
        }, 0f, 0f, 8f, 8f, 40f, 60f, 4, 4, false, false);
        TerrainOutdoorLight.Shade(albedo, new double[] { n.x, n.y, n.z }, new double[] { l.x, l.y, l.z }, new double[] { ambient.x, ambient.y, ambient.z },
                                  new double[] { horizon.x, horizon.y, horizon.z }, new double[] { ground.x, ground.y, ground.z },
                                  new double[] { direct.x, direct.y, direct.z }, flat, want);
        double worst = 0.0;
        for (int i = 0; i < 16; i++)
            for (int c = 0; c < 3; c++)
                worst = Math.Max(worst, Math.Abs(lit[4 * i + c] - want[c]));
        return worst;
    }

    // ---------------------------------------------------------------- step 4: the sky

    /// <summary>A sky input set for the fixtures: six distinct colours, a tint around a sun 12 deg up, its strengths apart above
    /// and below the horizon ramp (so the ramp's direction is tested too).</summary>
    static SkyInputs SkyFixtureInputs()
    {
        var s = new SkyInputs();
        double[] colours = { 0.10, 0.20, 0.55, 0.25, 0.40, 0.70, 0.60, 0.55, 0.50, 0.85, 0.65, 0.45, 0.80, 0.70, 0.60, 0.30, 0.25, 0.35 };
        Array.Copy(colours, s.Colours, colours.Length);
        s.Angle = 0.2;
        s.Tint[0] = 1.0;
        s.Tint[1] = 0.6;
        s.Tint[2] = 0.2;
        s.WeightAbove = 0.6;
        s.WeightBelow = 0.2;
        double e = 12.0 * Math.PI / 180.0;
        s.Sun[0] = Math.Cos(e);
        s.Sun[1] = Math.Sin(e);
        s.Sun[2] = 0.0;
        return s;
    }

    /// <summary>The dome drawn with `m` by a command buffer into a w x h float target whose depth is cleared to the far plane,
    /// seen along `forward` with a vertical field of view `fov`; the pixels bottom row first (Texture2D order), RGBA floats.</summary>
    static float[] SkyFixtureRender(Material m, Mesh dome, int w, int h, Vector3 forward, float fov, out Matrix4x4 view, out Matrix4x4 proj)
    {
        Quaternion rot = Quaternion.LookRotation(forward, Mathf.Abs(forward.normalized.y) > 0.99f ? Vector3.forward : Vector3.up);
        view = Matrix4x4.Scale(new Vector3(1f, 1f, -1f)) * Matrix4x4.Rotate(Quaternion.Inverse(rot));
        proj = Matrix4x4.Perspective(fov, (float)w / h, 0.05f, 10f);
        Matrix4x4 gpu = GL.GetGPUProjectionMatrix(proj, true);
        var desc = new RenderTextureDescriptor(w, h, GraphicsFormat.R32G32B32A32_SFloat, 24) { msaaSamples = 1, useMipMap = false };
        var target = new RenderTexture(desc) { name = "WmvSky test target" };
        target.Create();
        var readback = new Texture2D(w, h, TextureFormat.RGBAFloat, false, true) { name = "WmvSky test readback" };
        RenderTexture previous = RenderTexture.active;
        var cmd = new CommandBuffer { name = "WmvSky test" };
        int encodeId = Shader.PropertyToID("_WmvShaderEncode");
        float savedEncode = Shader.GetGlobalFloat(encodeId);
        try
        {
            cmd.SetRenderTarget(target);
            cmd.SetViewport(new Rect(0, 0, w, h));
            cmd.ClearRenderTarget(true, true, new Color(0f, 0f, 0f, 0f), 1f);
            cmd.SetGlobalFloat(encodeId, 0f);
            cmd.SetViewProjectionMatrices(view, proj);
            cmd.SetGlobalMatrix(Shader.PropertyToID("unity_MatrixV"), view);
            cmd.SetGlobalMatrix(Shader.PropertyToID("glstate_matrix_projection"), gpu);
            cmd.SetGlobalMatrix(Shader.PropertyToID("unity_MatrixVP"), gpu * view);
            cmd.DrawMesh(dome, Matrix4x4.identity, m, 0, 0);
            cmd.SetGlobalFloat(encodeId, savedEncode);
            Graphics.ExecuteCommandBuffer(cmd);
            RenderTexture.active = target;
            readback.ReadPixels(new Rect(0, 0, w, h), 0, 0, false);
            RenderTexture.active = previous;
            return readback.GetRawTextureData<float>().ToArray();
        }
        finally
        {
            RenderTexture.active = previous;
            Shader.SetGlobalFloat(encodeId, savedEncode);
            cmd.Release();
            UnityEngine.Object.DestroyImmediate(readback);
            target.Release();
            UnityEngine.Object.DestroyImmediate(target);
        }
    }

    /// <summary>The sky's GPU against its CPU twin (SkyDome.Trace on the same mesh arrays, the ray through each pixel's centre):
    /// a view along the horizon towards the sun (the tint, rings 1-5, the ramp, the bottom fan) and one straight up (the top
    /// fan), table A with ring 5 leaning to an end-fog colour; worst channel 3e-3 or less, and every pixel drawn (the depth was
    /// cleared to the far plane).</summary>
    static void OutdoorSkyFixtureTests(Action<string> log)
    {
        var made = new List<UnityEngine.Object>();
        try
        {
            Material m = WmvWorldSky.CreateMaterial();
            if (m == null)
            {
                Check(false, "sky fixture: Resources/" + WmvWorldSky.ShaderResource + ".shader is in the player build", log);
                return;
            }
            made.Add(m);
            Mesh dome = WmvWorldSky.BuildMesh(false);
            made.Add(dome);
            float[] p, slots;
            int[] ix;
            WmvWorldSky.DomeArrays(false, out p, out slots, out ix);
            SkyInputs s = SkyFixtureInputs();
            WmvWorldSky.SetInputs(m, s);
            m.SetFloat(WmvWorldSky.LinearOutId, 0f);
            const int W = 96, H = 64;
            double worst = 0.0;
            int drawn = 0, missed = 0, total = 0;
            string at = "";
            foreach (Vector3 forward in new[] { new Vector3(1f, 0.05f, 0.2f), new Vector3(0f, 1f, 0f) })
            {
                Matrix4x4 view, proj;
                float[] px = SkyFixtureRender(m, dome, W, H, forward, 100f, out view, out proj);
                Matrix4x4 toWorld = view.inverse;
                for (int yy = 0; yy < H; yy++)
                    for (int xx = 0; xx < W; xx++)
                    {
                        total++;
                        int i = 4 * (yy * W + xx);
                        if (px[i + 3] == 1f)
                            drawn++;
                        double nx = (xx + 0.5) / W * 2.0 - 1.0, ny = (yy + 0.5) / H * 2.0 - 1.0;
                        Vector3 d = toWorld.MultiplyVector(new Vector3((float)(nx / proj.m00), (float)(ny / proj.m11), -1f));
                        double r, g, b;
                        if (!SkyDome.Trace(p, slots, ix, d.x, d.y, d.z, s, out r, out g, out b))
                        {
                            missed++;
                            continue;
                        }
                        double e = Math.Max(Math.Abs(px[i] - r), Math.Max(Math.Abs(px[i + 1] - g), Math.Abs(px[i + 2] - b)));
                        if (e > worst)
                        {
                            worst = e;
                            at = "view " + forward + " pixel (" + xx + ", " + yy + ")";
                        }
                    }
            }
            Check(worst <= 3e-3 && drawn == total && missed == 0,
                  string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                "sky fixture: the GPU's dome (Resources/WmvSky.shader) against its CPU twin (SkyDome.Trace) over {0} pixels of two " +
                                "views (along the horizon towards the sun, straight up): worst channel {1:0.#######} at {2} (3e-3); {3} drawn over the " +
                                "far-plane depth, {4} rays missed", total, worst, at, drawn, missed), log);
        }
        catch (Exception e)
        {
            Check(false, "sky fixture: the test threw " + e.GetType().Name + ": " + e.Message, log);
        }
        finally
        {
            foreach (UnityEngine.Object x in made)
                if (x != null)
                    UnityEngine.Object.DestroyImmediate(x);
        }
    }

    /// <summary>The sky on Camera.main's world view (a terrain-layer floor under the bottom half, the sky enqueued by a probe
    /// for this camera, the water's grab forced on with its depth and coverage copied out): recorded once and BEFORE the grab
    /// (its event is earlier: the order holds whatever the enqueue order); the depth at the sky's pixels still exactly the far
    /// plane and the grab's coverage 0 there, 1 on the floor; the floor's pixels byte-identical with and without the sky, the
    /// background's not; and which cameras get it (not another camera, not the orthographic overview, not outside the world
    /// view). Camera.main's state, the grab's installation and every object are put back.</summary>
    static void OutdoorSkyCameraTests(Action<string> log)
    {
        Camera cam = Camera.main;
        Shader terrainShader = WmvTerrainBuilder.ResolveShader(null);
        if (cam == null || terrainShader == null)
        {
            log("lifecycle-test SKIP: the sky on Camera.main: " + (cam == null ? "no main camera yet" : "a shader is missing"));
            return;
        }
        const int Size = 64;
        Vector3 origin = new Vector3(-21000f, 20000f, -21000f);
        bool grabWasActive = WmvWaterGrabPass.Active;
        Vector3 pos = cam.transform.position;
        Quaternion rot = cam.transform.rotation;
        RenderTexture target0 = cam.targetTexture;
        int mask0 = cam.cullingMask;
        float fov0 = cam.fieldOfView, near0 = cam.nearClipPlane, far0 = cam.farClipPlane;
        bool ortho0 = cam.orthographic;
        CameraClearFlags clear0 = cam.clearFlags;
        Color background0 = cam.backgroundColor;
        UnityEngine.Rendering.Universal.UniversalAdditionalCameraData camData;
        if (!cam.TryGetComponent(out camData))
            camData = null;
        bool post0 = camData != null && camData.renderPostProcessing;
        RenderTexture previous = RenderTexture.active;
        var made = new List<UnityEngine.Object>();
        RenderTexture target = null, depthCopy = null, reflCopy = null;
        Action<ScriptableRenderContext, Camera> probe = null;
        bool skyMaterialBefore = WmvWorldSky.MaterialCount > 0;
        try
        {
            // ---- which cameras get it
            var otherGo = new GameObject("WmvSky test other camera");
            made.Add(otherGo);
            Camera other = otherGo.AddComponent<Camera>();
            other.enabled = false;
            other.cullingMask = WmvTerrainBuilder.TerrainViewMask;
            cam.cullingMask = WmvTerrainBuilder.TerrainViewMask;
            cam.orthographic = false;
            bool wantsMain = WmvWorldSky.Wants(cam, true);
            bool notOther = !WmvWorldSky.Wants(other, true);
            bool notOutside = !WmvWorldSky.Wants(cam, false);
            cam.orthographic = true;
            bool notOrtho = !WmvWorldSky.Wants(cam, true);
            cam.orthographic = false;
            cam.cullingMask = WmvTerrainBuilder.ModelViewMask;
            bool notModels = !WmvWorldSky.Wants(cam, true);
            Check(wantsMain && notOther && notOutside && notOrtho && notModels &&
                  (int)WmvWorldSky.Event < (int)UnityEngine.Rendering.Universal.RenderPassEvent.BeforeRenderingTransparents,
                  "sky: only Camera.main's perspective world view gets it (not another camera, not outside the world view, not the orthographic " +
                  "overview, not the Models mask); its event (" + WmvWorldSky.Event + ") comes before the water grab's (BeforeRenderingTransparents)", log);

            // ---- the real render
            Material ground = new Material(terrainShader) { name = "WmvSky test ground" };
            made.Add(ground);
            ground.SetFloat("_Cull", (float)CullMode.Off);
            var mesh = new Mesh { name = "WmvSky test floor" };
            made.Add(mesh);
            // The floor under the bottom half of the picture (the camera looks north, level): y -2, from 2 yd to 40 yd ahead.
            mesh.SetVertices(new List<Vector3> { new Vector3(-60f, -2f, 2f), new Vector3(60f, -2f, 2f), new Vector3(60f, -2f, 40f), new Vector3(-60f, -2f, 40f) });
            mesh.SetNormals(new List<Vector3> { Vector3.up, Vector3.up, Vector3.up, Vector3.up });
            mesh.SetIndices(new[] { 0, 2, 1, 0, 3, 2 }, MeshTopology.Triangles, 0);
            mesh.RecalculateBounds();
            var floor = new GameObject("WmvSky test floor") { layer = WmvTerrainBuilder.Layer };
            made.Add(floor);
            floor.transform.position = origin;
            floor.AddComponent<MeshFilter>().sharedMesh = mesh;
            MeshRenderer mr = floor.AddComponent<MeshRenderer>();
            mr.sharedMaterial = ground;
            WmvLiquidMeshes.Quiet(mr);
            target = new RenderTexture(Size, Size, 24, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear) { name = "WmvSky test target" };
            target.Create();
            depthCopy = new RenderTexture(Size, Size, 0, RenderTextureFormat.RFloat, RenderTextureReadWrite.Linear) { name = "WmvSky test depth" };
            depthCopy.Create();
            reflCopy = new RenderTexture(Size, Size, 0, RenderTextureFormat.ARGBFloat, RenderTextureReadWrite.Linear) { name = "WmvSky test coverage" };
            reflCopy.Create();
            cam.transform.position = origin;
            cam.transform.rotation = Quaternion.LookRotation(Vector3.forward, Vector3.up);
            cam.fieldOfView = 60f;
            cam.nearClipPlane = 0.25f;
            cam.farClipPlane = 100f;
            cam.clearFlags = CameraClearFlags.SolidColor;
            cam.backgroundColor = new Color(0.1f, 0.1f, 0.12f, 1f);
            cam.targetTexture = target;
            cam.cullingMask = WmvTerrainBuilder.TerrainViewMask;
            if (camData != null)
                camData.renderPostProcessing = false;
            SkyInputs s = SkyFixtureInputs();
            bool withSky = false;
            probe = (ctx, c) =>
            {
                if (withSky && c == cam)
                    WmvWorldSky.Enqueue(c, true, s, false, null);
            };
            RenderPipelineManager.beginCameraRendering += probe;
            WmvWaterGrabPass.SetActive(true);
            WmvWaterGrabPass.ForceForTest = true;
            Func<Color32[]> render = () =>
            {
                cam.Render();
                RenderTexture.active = target;
                var tex = new Texture2D(Size, Size, TextureFormat.RGBA32, false, true);
                tex.ReadPixels(new Rect(0, 0, Size, Size), 0, 0);
                tex.Apply(false);
                RenderTexture.active = previous;
                Color32[] px = tex.GetPixels32();
                UnityEngine.Object.DestroyImmediate(tex);
                return px;
            };
            Func<float[]> readDepth = () =>
            {
                RenderTexture.active = depthCopy;
                var dt = new Texture2D(Size, Size, TextureFormat.RFloat, false, true);
                dt.ReadPixels(new Rect(0, 0, Size, Size), 0, 0);
                dt.Apply(false);
                RenderTexture.active = previous;
                float[] d = dt.GetRawTextureData<float>().ToArray();
                UnityEngine.Object.DestroyImmediate(dt);
                return d;
            };
            // Without the sky first (the depth after the opaque world: the floor and the far plane), then with it.
            WmvWaterGrabPass.CaptureDepthForTest(depthCopy);
            Color32[] plain = render();
            float[] plainDepths = readDepth();
            withSky = true;
            WmvWaterGrabPass.CaptureReflectionForTest(reflCopy);
            int records0 = WmvWorldSky.Records;
            Color32[] sky = render();
            int records = WmvWorldSky.Records - records0;
            bool order = WmvWorldSky.LastRecordFrame == WmvWaterGrabPass.LastFrame && WmvWaterGrabPass.Runs == WmvWorldSky.GrabRunsAtRecord + 1;
            WmvWaterGrabPass.CaptureDepthForTest(null);
            WmvWaterGrabPass.CaptureReflectionForTest(null);
            float[] depths = readDepth();
            RenderTexture.active = reflCopy;
            var rt = new Texture2D(Size, Size, TextureFormat.RGBAFloat, false, true);
            rt.ReadPixels(new Rect(0, 0, Size, Size), 0, 0);
            rt.Apply(false);
            Color[] refl = rt.GetPixels();
            UnityEngine.Object.DestroyImmediate(rt);
            RenderTexture.active = previous;
            float farRaw = SystemInfo.usesReversedZBuffer ? 0f : 1f;
            int floorN = 0, skyN = 0, floorSame = 0, skyChanged = 0, skyFar = 0, skyCover0 = 0, floorCover1 = 0, depthSame = 0;
            for (int i = 0; i < Size * Size; i++)
            {
                if (depths[i] == plainDepths[i])
                    depthSame++;
                bool isFloor = plainDepths[i] != farRaw;            // by the render WITHOUT the sky
                if (isFloor)
                {
                    floorN++;
                    if (sky[i].r == plain[i].r && sky[i].g == plain[i].g && sky[i].b == plain[i].b)
                        floorSame++;
                    if (refl[i].a == 1f)
                        floorCover1++;
                }
                else
                {
                    skyN++;
                    if (depths[i] == farRaw)
                        skyFar++;
                    if (sky[i].r != plain[i].r || sky[i].g != plain[i].g || sky[i].b != plain[i].b)
                        skyChanged++;
                    if (refl[i].a == 0f)
                        skyCover0++;
                }
            }
            bool halves = floorN > Size * Size / 4 && skyN > Size * Size / 4;
            Check(records == 1 && order && halves && depthSame == Size * Size && floorSame == floorN && skyChanged == skyN && skyFar == skyN &&
                  skyCover0 == skyN && floorCover1 == floorN,
                  "sky on Camera.main: recorded once and before the water's grab in the same frame (" + order + "); the depth after it equal to the " +
                  "depth without it at every pixel (" + depthSame + " of " + (Size * Size) + "); the background's " + skyN + " pixels all changed " +
                  "from the clear colour (" + skyChanged + "), their depth still exactly the far plane (" + skyFar + ") and the grab's coverage 0 " +
                  "there (" + skyCover0 + "); the floor's " + floorN + " pixels unchanged (" + floorSame + ") with coverage 1 (" + floorCover1 + ")", log);

            // ---- the dome's mesh and material go with the scene (review finding 6): WmvWorldLight.Off -- which clearTerrain, a
            // model load, a new scene, setWorldLight off and a fault all take -- destroys them at once.
            bool madeBySky = WmvWorldSky.MaterialCount == 1 && WmvWorldSky.MeshCount == 1;
            int meshesWith = Resources.FindObjectsOfTypeAll<Mesh>().Length, materialsWith = Resources.FindObjectsOfTypeAll<Material>().Length;
            WmvWorldLight.Off();
            int meshesAfter = Resources.FindObjectsOfTypeAll<Mesh>().Length, materialsAfter = Resources.FindObjectsOfTypeAll<Material>().Length;
            Check(madeBySky && WmvWorldSky.MaterialCount == 0 && WmvWorldSky.MeshCount == 0 && meshesAfter == meshesWith - 1 &&
                  materialsAfter == materialsWith - 1,
                  "sky: its dome mesh and material (made at the first draw) go with the scene and when the light goes off (WmvWorldLight.Off): " +
                  "Unity's mesh and material counts -- runtimeState's liveMeshes and liveMaterials -- drop by one each at once (" + meshesWith + " -> " +
                  meshesAfter + ", " + materialsWith + " -> " + materialsAfter + ")", log);
        }
        catch (Exception e)
        {
            Check(false, "sky on Camera.main: the test threw " + e.GetType().Name + ": " + e.Message, log);
        }
        finally
        {
            if (probe != null)
                RenderPipelineManager.beginCameraRendering -= probe;
            WmvWaterGrabPass.ForceForTest = false;
            WmvWaterGrabPass.CaptureDepthForTest(null);
            WmvWaterGrabPass.CaptureReflectionForTest(null);
            WmvWaterGrabPass.SetActive(grabWasActive);
            cam.targetTexture = target0;
            cam.cullingMask = mask0;
            cam.transform.position = pos;
            cam.transform.rotation = rot;
            cam.fieldOfView = fov0;
            cam.nearClipPlane = near0;
            cam.farClipPlane = far0;
            cam.orthographic = ortho0;
            cam.clearFlags = clear0;
            cam.backgroundColor = background0;
            if (camData != null)
                camData.renderPostProcessing = post0;
            RenderTexture.active = previous;
            if (target != null) { target.Release(); UnityEngine.Object.DestroyImmediate(target); }
            if (depthCopy != null) { depthCopy.Release(); UnityEngine.Object.DestroyImmediate(depthCopy); }
            if (reflCopy != null) { reflCopy.Release(); UnityEngine.Object.DestroyImmediate(reflCopy); }
            foreach (UnityEngine.Object o in made)
                if (o != null)
                    UnityEngine.Object.DestroyImmediate(o);
            if (!skyMaterialBefore)
                WmvWorldSky.DropResources();
        }
    }

    /// <summary>Bloom (plan D9): two volumes of the test's own (Bloom 0.7 overridden, Bloom 0.3 not overridden) -- held at 0 and
    /// overridden while the light is on in the world view (WmvWorldLight.Sky), put back exactly on leaving the world view, and
    /// on switching the light off; never touched outside the world view or with the light off.</summary>
    static void OutdoorBloomTests(Action<string> log)
    {
        var made = new List<UnityEngine.Object>();
        var volumes = new List<Volume>();
        bool wasInstalled = WmvWorldLight.Installed;
        object standIn = new object();
        try
        {
            if (!wasInstalled)
                WmvWorldLight.Configure(standIn, null);
            Func<float, bool, UnityEngine.Rendering.Universal.Bloom> volume = (value, overridden) =>
            {
                var go = new GameObject("WmvSky test volume");
                made.Add(go);
                Volume v = go.AddComponent<Volume>();
                volumes.Add(v);
                v.isGlobal = true;
                var profile = ScriptableObject.CreateInstance<VolumeProfile>();
                made.Add(profile);
                UnityEngine.Rendering.Universal.Bloom bloom = profile.Add<UnityEngine.Rendering.Universal.Bloom>(false);
                bloom.intensity.overrideState = overridden;
                bloom.intensity.value = value;
                v.sharedProfile = profile;
                UnityEngine.Rendering.Universal.Bloom live;
                v.profile.TryGet(out live);
                return live;
            };
            UnityEngine.Rendering.Universal.Bloom a = volume(0.7f, true), b = volume(0.3f, false);
            Camera cam = Camera.main;
            WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasOn = true, On = false });
            WmvWorldLight.Sky(cam, true);                            // light off in the world view: untouched
            bool offUntouched = !WmvWorldSky.BloomHeld && a.intensity.value == 0.7f && b.intensity.value == 0.3f;
            WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasOn = true, On = true });
            WmvWorldLight.Sky(cam, false);                           // on, outside the world view: untouched
            bool outsideUntouched = !WmvWorldSky.BloomHeld && a.intensity.value == 0.7f && a.intensity.overrideState;
            WmvWorldLight.Sky(cam, true);                            // on in the world view: held at 0
            bool held = WmvWorldSky.BloomHeld && a.intensity.value == 0f && a.intensity.overrideState && b.intensity.value == 0f &&
                        b.intensity.overrideState;
            WmvWorldLight.Sky(cam, false);                           // leaving the world view: put back exactly
            bool leave = !WmvWorldSky.BloomHeld && a.intensity.value == 0.7f && a.intensity.overrideState && b.intensity.value == 0.3f &&
                         !b.intensity.overrideState;
            WmvWorldLight.Sky(cam, true);
            WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasOn = true, On = false });    // switching the light off
            bool switchedOff = !WmvWorldSky.BloomHeld && a.intensity.value == 0.7f && a.intensity.overrideState && b.intensity.value == 0.3f &&
                               !b.intensity.overrideState;
            Check(offUntouched && outsideUntouched && held && leave && switchedOff,
                  "bloom: untouched with the light off and outside the world view; held at 0 (overridden) while the light is on in the world view; " +
                  "put back exactly (0.7 overridden, 0.3 not overridden) on leaving the world view and on switching the light off", log);
        }
        catch (Exception e)
        {
            Check(false, "bloom: the test threw " + e.GetType().Name + ": " + e.Message, log);
        }
        finally
        {
            WmvWorldSky.HoldBloom(false, null);
            // The volumes' own profile copies (Volume.profile makes one), then everything made.
            foreach (Volume v in volumes)
                if (v != null && v.HasInstantiatedProfile())
                    UnityEngine.Object.DestroyImmediate(v.profile);
            foreach (UnityEngine.Object o in made)
                if (o != null)
                    UnityEngine.Object.DestroyImmediate(o);
            if (!wasInstalled && WmvWorldLight.Owner == standIn)
                WmvWorldLight.Release(standIn);
            WmvWorldLight.ApplySwitches();
            WmvWorldLight.Off();
        }
    }

    // ---------------------------------------------------------------- step 6: world models and doodads (T1-T15)

    static readonly int RigId = Shader.PropertyToID("_WmvRig");

    /// <summary>A probe's outdoor light: the four colours (authored) and the direction towards the light (Unity, unit), as the
    /// probe's command buffer sets them; Twin is the CPU's colour of a pixel (ModelOutdoorLight).</summary>
    sealed class ModelProbeLight
    {
        public readonly Vector4 Ambient, Horizon, Ground, Direct;
        public readonly Vector3 L;

        public ModelProbeLight(Vector4 ambient, Vector4 horizon, Vector4 ground, Vector4 direct, Vector3 towards)
        {
            Ambient = ambient;
            Horizon = horizon;
            Ground = ground;
            Direct = direct;
            L = towards.normalized;
        }

        static double[] D(Vector4 v) { return new double[] { v.x, v.y, v.z }; }

        /// <summary>ShadeWmo (the world model: albedo x (1, 0.99998, 1)) or ShadeM2 of `albedo` on the interpolant `n`.</summary>
        public double[] Twin(bool wmo, double[] albedo, double[] n, bool flat)
        {
            var rgb = new double[3];
            double[] l = { L.x, L.y, L.z };
            if (wmo)
                ModelOutdoorLight.ShadeWmo(albedo, n, l, D(Ambient), D(Horizon), D(Ground), D(Direct), flat, rgb);
            else
                ModelOutdoorLight.ShadeM2(albedo, n, l, D(Ambient), D(Horizon), D(Ground), D(Direct), flat, rgb);
            return rgb;
        }
    }

    /// <summary>A probe quad over the composite's 8 x 8 yd window at height 50: vertex normal n0 at x = 0 and n1 at x = 8 -- the
    /// pixel of column i (x = i + 0.5) gets lerp(n0, n1, (i + 0.5) / 8) of the vertex program's normalised normals (the
    /// projection is orthographic, so the interpolation is linear) -- and UV0 u = x / 8 (the 2 x 1 probe texture: columns 0-3
    /// texel 0, 4-7 texel 1); `reversed` the other winding.</summary>
    static Mesh ModelProbeQuad(Vector3 n0, Vector3 n1, bool reversed, List<UnityEngine.Object> made)
    {
        var quad = new Mesh { name = "WmvWorldLit probe quad" };
        made.Add(quad);
        quad.SetVertices(new List<Vector3> { new Vector3(0f, 50f, 0f), new Vector3(8f, 50f, 0f), new Vector3(8f, 50f, -8f), new Vector3(0f, 50f, -8f) });
        quad.SetNormals(new List<Vector3> { n0, n1, n1, n0 });
        quad.SetUVs(0, new List<Vector2> { new Vector2(0f, 0f), new Vector2(1f, 0f), new Vector2(1f, 1f), new Vector2(0f, 1f) });
        quad.SetIndices(reversed ? new[] { 0, 2, 1, 0, 3, 2 } : new[] { 0, 1, 2, 0, 2, 3 }, MeshTopology.Triangles, 0);
        quad.RecalculateBounds();
        return quad;
    }

    /// <summary>The raw interpolant at probe column i of a ModelProbeQuad (n0, n1).</summary>
    static double[] ModelProbeNormal(Vector3 n0, Vector3 n1, int column)
    {
        double t = (column + 0.5) / 8.0;
        Vector3 a = n0.normalized, b = n1.normalized;
        return new[] { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
    }

    /// <summary>A 2 x 1 texture, point-sampled and clamped, uploaded undecoded as the builders upload theirs (linear: the shader
    /// reads byte / 255).</summary>
    static Texture2D ModelProbeTexture(Color32 left, Color32 right, List<UnityEngine.Object> made)
    {
        var t = new Texture2D(2, 1, TextureFormat.RGBA32, false, true)
        {
            name = "WmvWorldLit probe texture", filterMode = FilterMode.Point, wrapMode = TextureWrapMode.Clamp,
        };
        t.SetPixels32(new[] { left, right });
        t.Apply(false);
        made.Add(t);
        return t;
    }

    static double[] ModelProbeAlbedo(Color32 c) { return new[] { c.r / 255.0, c.g / 255.0, c.b / 255.0 }; }

    /// <summary>A minimal M2 original as the builder makes one: `tex` on unit 0, combiner 0, alpha from unit 0 at
    /// scale 1, _Color white, opaque alpha, culling off.</summary>
    static Material ModelProbeM2(Shader s, Texture2D tex, string name, List<UnityEngine.Object> made)
    {
        var m = new Material(s) { name = name };
        made.Add(m);
        m.SetTexture("_MainTex", tex);
        m.SetFloat("_CombinerMode", 0f);
        m.SetFloat("_AlphaMode", 1f);
        m.SetFloat("_AlphaScale", 1f);
        m.SetColor("_Color", Color.white);
        m.SetFloat("_OpaqueAlpha", 1f);
        m.SetFloat("_Cull", (float)CullMode.Off);
        return m;
    }

    /// <summary>A minimal world-model original: permutation 1 (diffuse t0), `tex` in register t0 on UV channel 0, no alpha
    /// test, culling off.</summary>
    static Material ModelProbeWmo(Shader s, Texture2D tex, string name, List<UnityEngine.Object> made)
    {
        var m = new Material(s) { name = name };
        made.Add(m);
        m.SetFloat("_WmoPermutation", 1f);
        m.SetTexture("_WmoTex0", tex);
        m.SetFloat("_WmoUv0", 0f);
        m.SetFloat("_AlphaTest", 0f);
        m.SetFloat("_Cull", (float)CullMode.Off);
        return m;
    }

    static Material ModelProbeCopy(Material original, Shader variant, List<UnityEngine.Object> made)
    {
        Material c = WmvWorldLitMaterials.Copy(original, variant);
        made.Add(c);
        return c;
    }

    /// <summary>`quad` drawn with `m` inside WmvTerrainComposite's render (8 x 2 pixels over the window; _WmvShaderEncode 0 and
    /// _WmvWLOn pinned 0 by the composite); with `l`, the probe's own command buffer sets the light on with its colours and
    /// direction after the pin; `models` is _WmvWLModels and `rig` _WmvRig for the draw, both put back after it. Returns the
    /// 16 pixels' RGBA (rows north first).</summary>
    static float[] ModelProbe(Material m, Mesh quad, ModelProbeLight l, Vector4 models, float rig = 0f)
    {
        float rigWas = Shader.GetGlobalFloat(RigId);
        Vector4 modelsWas = Shader.GetGlobalVector(WmvWorldLight.ModelsId);
        return WmvTerrainComposite.RenderToArray(cmd =>
        {
            if (l != null)
            {
                cmd.SetGlobalFloat(WmvWorldLight.OnId, 1f);
                cmd.SetGlobalVector(WmvWorldLight.LightDirId, new Vector4(l.L.x, l.L.y, l.L.z, 0f));
                cmd.SetGlobalVector(WmvWorldLight.AmbientId, l.Ambient);
                cmd.SetGlobalVector(WmvWorldLight.HorizonAmbientId, l.Horizon);
                cmd.SetGlobalVector(WmvWorldLight.GroundAmbientId, l.Ground);
                cmd.SetGlobalVector(WmvWorldLight.DirectId, l.Direct);
            }
            cmd.SetGlobalVector(WmvWorldLight.ModelsId, models);
            cmd.SetGlobalFloat(RigId, rig);
            cmd.DrawMesh(quad, Matrix4x4.identity, m, 0, 0);
            cmd.SetGlobalFloat(RigId, rigWas);
            cmd.SetGlobalVector(WmvWorldLight.ModelsId, modelsWas);
        }, 0f, 0f, 8f, 8f, 40f, 60f, 8, 2, false, false);
    }

    /// <summary>The worst channel of a probe's pixels against `want` (per column; null: the column is not compared), and every
    /// compared pixel's alpha 1.</summary>
    static double ModelProbeWorst(float[] px, Func<int, double[]> want, out bool alphaOne)
    {
        double worst = 0.0;
        alphaOne = true;
        for (int row = 0; row < 2; row++)
            for (int c = 0; c < 8; c++)
            {
                double[] w = want(c);
                if (w == null)
                    continue;
                int i = 4 * (row * 8 + c);
                for (int k = 0; k < 3; k++)
                    worst = Math.Max(worst, Math.Abs(px[i + k] - w[k]));
                alphaOne &= px[i + 3] == 1f;
            }
        return worst;
    }

    static double ModelProbeWorst(float[] px, Func<int, double[]> want)
    {
        bool unused;
        return ModelProbeWorst(px, want, out unused);
    }

    /// <summary>The pixels of two probes that are exactly equal (all four channels), of 16.</summary>
    static int ModelProbeSame(float[] a, float[] b)
    {
        int n = 0;
        for (int i = 0; i + 3 < a.Length && i + 3 < b.Length; i += 4)
            if (a[i] == b[i] && a[i + 1] == b[i + 1] && a[i + 2] == b[i + 2] && a[i + 3] == b[i + 3])
                n++;
        return n;
    }

    static double ModelProbeDiff(float[] a, float[] b)
    {
        double worst = 0.0;
        for (int i = 0; i < a.Length && i < b.Length; i++)
            worst = Math.Max(worst, Math.Abs(a[i] - b[i]));
        return worst;
    }

    static string F6(double v) { return v.ToString("0.######", System.Globalization.CultureInfo.InvariantCulture); }

    static void OutdoorModelTests(Action<string> log)
    {
        try { OutdoorModelShaderTests(log); }
        catch (Exception e) { Check(false, "outdoor models: the shader tests threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log); }
        try { OutdoorModelWorldModelTests(log); }
        catch (Exception e) { Check(false, "outdoor models: the world-model binding tests threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log); }
        try { OutdoorModelDoodadTests(log); }
        catch (Exception e) { Check(false, "outdoor models: the doodad binding tests threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log); }
        try { OutdoorModelDriverTests(log); }
        catch (Exception e) { Check(false, "outdoor models: the driver tests threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log); }
    }

    /// <summary>T1-T10: the world-lit variants on the GPU, each probe drawn by a copy made through WmvWorldLitMaterials.Copy from
    /// a minimal original, against the CPU twin (ModelOutdoorLight) within 2e-3 (half floats) or exactly where the shader returns
    /// what it read. Every global touched is put back.</summary>
    static void OutdoorModelShaderTests(Action<string> log)
    {
        var made = new List<UnityEngine.Object>();
        int[] ids = { WmvWorldLight.ModelsId, WmvWorldLight.LightDirId, WmvWorldLight.AmbientId, WmvWorldLight.HorizonAmbientId,
                      WmvWorldLight.GroundAmbientId, WmvWorldLight.DirectId };
        var saved = new Vector4[ids.Length];
        for (int k = 0; k < ids.Length; k++)
            saved[k] = Shader.GetGlobalVector(ids[k]);
        float savedOn = Shader.GetGlobalFloat(WmvWorldLight.OnId), savedRig = Shader.GetGlobalFloat(RigId);
        try
        {
            Shader m2Shader = WmvModelBuilder.ResolveShader(null), wmoShader = WmvWmoBuilder.ResolveMapObjectShader(null);
            var texel0 = new Color32(153, 115, 77, 255);
            var texel1 = new Color32(90, 160, 200, 255);
            Texture2D tex = ModelProbeTexture(texel0, texel1, made);
            double[][] albedo = { ModelProbeAlbedo(texel0), ModelProbeAlbedo(texel1) };
            Func<int, double[]> albedoAt = c => albedo[c < 4 ? 0 : 1];
            bool shaders = m2Shader != null && m2Shader.name == WmvWorldLitMaterials.DoodadShaderName && wmoShader != null &&
                           wmoShader.name == WmvWorldLitMaterials.WorldModelShaderName;
            Material m2 = shaders ? ModelProbeM2(m2Shader, tex, "WmvWorldLit probe (M2)", made) : null;
            Material wmo = shaders ? ModelProbeWmo(wmoShader, tex, "WmvWorldLit probe (WMO)", made) : null;
            Shader m2Variant = shaders ? WmvWorldLitMaterials.ResolveVariant(true, m2, log) : null;
            Shader wmoVariant = shaders ? WmvWorldLitMaterials.ResolveVariant(false, wmo, log) : null;
            Check(shaders && m2Variant != null && wmoVariant != null && m2Variant.name == WmvWorldLitMaterials.DoodadShaderName + WmvWorldLitMaterials.Suffix &&
                  wmoVariant.name == WmvWorldLitMaterials.WorldModelShaderName + WmvWorldLitMaterials.Suffix && m2Variant.isSupported && wmoVariant.isSupported &&
                  WmvWorldLitMaterials.ResolveVariant(false, m2, null) == null && WmvWorldLitMaterials.ResolveVariant(true, wmo, null) == null,
                  "outdoor models: the originals' shaders (" + (m2Shader != null ? m2Shader.name : "none") + ", " + (wmoShader != null ? wmoShader.name : "none") +
                  ") and their world-lit variants (" + (m2Variant != null ? m2Variant.name : "MISSING") + ", " + (wmoVariant != null ? wmoVariant.name : "MISSING") +
                  ") are in the player build and supported; a variant is chosen by (kind, shader name) only -- a doodad's map-object shader and a " +
                  "world model's M2 shader have none", log);
            if (m2Variant == null || wmoVariant == null)
                return;
            Material m2Lit = ModelProbeCopy(m2, m2Variant, made), wmoLit = ModelProbeCopy(wmo, wmoVariant, made);
            Vector4 none = Vector4.zero;

            // ---- T1: the raw interpolated normal -- lengths down to about 0.71 between two unit normals 90 deg apart: H and the
            // wrap from the normalised vector, the direct term from the raw one; then the terrain probe's other paths.
            var ambient = new Vector4(0.25f, 0.30f, 0.40f, 1f);
            var horizon = new Vector4(0.50f, 0.20f, 0.10f, 1f);
            var ground = new Vector4(0.05f, 0.10f, 0.15f, 1f);
            var direct = new Vector4(0.60f, 0.30f, 0.0f, 1f);
            var black = new Vector4(0f, 0f, 0f, 1f);
            var light = new ModelProbeLight(ambient, horizon, ground, direct, new Vector3(0.5f, 0.6f, 0.3f));
            Vector3 n0 = new Vector3(0.6f, 0.8f, 0f), n1 = new Vector3(-0.48f, 0.36f, 0.8f);
            Mesh quad = ModelProbeQuad(n0, n1, false, made);
            Func<bool, ModelProbeLight, bool, Func<int, double[]>> twinOf = (isWmo, l, flat) => c => l.Twin(isWmo, albedoAt(c), ModelProbeNormal(n0, n1, c), flat);
            bool alphaM2, alphaWmo;
            double e1M2 = ModelProbeWorst(ModelProbe(m2Lit, quad, light, none), twinOf(false, light, false), out alphaM2);
            double e1Wmo = ModelProbeWorst(ModelProbe(wmoLit, quad, light, none), twinOf(true, light, false), out alphaWmo);
            double minLength = 1.0, rawVsUnit = 0.0;
            for (int c = 0; c < 8; c++)
            {
                double[] n = ModelProbeNormal(n0, n1, c);
                double len = Math.Sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                minLength = Math.Min(minLength, len);
                double[] raw = light.Twin(false, albedoAt(c), n, false), unit = light.Twin(false, albedoAt(c), new[] { n[0] / len, n[1] / len, n[2] / len }, false);
                for (int k = 0; k < 3; k++)
                    rawVsUnit = Math.Max(rawVsUnit, Math.Abs(raw[k] - unit[k]));
            }
            // A downward normal with a zero ground colour (lit from below: N.L > 0); a normal facing away from the light with a
            // zero horizon colour (no direct light, the wrap below 0.9).
            Vector3 down = new Vector3(0.1f, -0.7f, 0.3f), away = new Vector3(0.3f, 0.6f, -0.5f);
            var downLight = new ModelProbeLight(ambient, horizon, black, direct, new Vector3(0.2f, -0.4f, 0.9f));
            var awayLight = new ModelProbeLight(ambient, black, ground, direct, new Vector3(-0.4f, 0.3f, 0.8f));
            Mesh downQuad = ModelProbeQuad(down, down, false, made), awayQuad = ModelProbeQuad(away, away, false, made);
            Func<Vector3, ModelProbeLight, bool, Func<int, double[]>> uniform = (nn, l, isWmo) => c => l.Twin(isWmo, albedoAt(c), ModelProbeNormal(nn, nn, c), false);
            double eDown = Math.Max(ModelProbeWorst(ModelProbe(m2Lit, downQuad, downLight, none), uniform(down, downLight, false)),
                                    ModelProbeWorst(ModelProbe(wmoLit, downQuad, downLight, none), uniform(down, downLight, true)));
            double eAway = Math.Max(ModelProbeWorst(ModelProbe(m2Lit, awayQuad, awayLight, none), uniform(away, awayLight, false)),
                                    ModelProbeWorst(ModelProbe(wmoLit, awayQuad, awayLight, none), uniform(away, awayLight, true)));
            double nlDown = Vector3.Dot(down.normalized, downLight.L), nlAway = Vector3.Dot(away.normalized, awayLight.L);
            Check(e1M2 <= 2e-3 && e1Wmo <= 2e-3 && eDown <= 2e-3 && eAway <= 2e-3 && alphaM2 && alphaWmo && minLength < 0.72 && rawVsUnit > 0.01 &&
                  nlDown > 0.0 && nlAway < 0.0,
                  "outdoor models T1: the world-lit variants against the CPU twin, worst channel each (2e-3) -- the M2 variant " + F6(e1M2) +
                  ", the world model's " + F6(e1Wmo) + " on a quad whose interpolated normal runs between two unit normals 90 deg apart " +
                  "(lengths down to " + minLength.ToString("0.###") + "; H and the wrap normalised, the direct term on the raw vector -- the twin " +
                  "on the normalised vector differs by up to " + F6(rawVsUnit) + "); a downward normal with a zero ground colour " + F6(eDown) +
                  ", a normal facing away from the light (N.L " + nlAway.ToString("0.###") + ") with a zero horizon colour " + F6(eAway) +
                  "; alpha 1", log);

            // ---- T2: the flat arms (_WmvWLModels.x doodads, .y world models), each its own kind's only
            var flatLight = new ModelProbeLight(new Vector4(0.45f, 0.30f, 0.40f, 1f), horizon, ground, new Vector4(1.0f, 0.30f, 0.0f, 1f),
                                                new Vector3(0.5f, 0.6f, 0.3f));
            double f2M2 = ModelProbeWorst(ModelProbe(m2Lit, quad, flatLight, new Vector4(1f, 0f, 0f, 0f)), twinOf(false, flatLight, true));
            double f2Wmo = ModelProbeWorst(ModelProbe(wmoLit, quad, flatLight, new Vector4(0f, 1f, 0f, 0f)), twinOf(true, flatLight, true));
            double crossM2 = ModelProbeWorst(ModelProbe(m2Lit, quad, flatLight, new Vector4(0f, 1f, 0f, 0f)), twinOf(false, flatLight, false));
            double crossWmo = ModelProbeWorst(ModelProbe(wmoLit, quad, flatLight, new Vector4(1f, 0f, 0f, 0f)), twinOf(true, flatLight, false));
            bool saturates = false;
            for (int c = 0; c < 8; c++)
            {
                double[] n = ModelProbeNormal(n0, n1, c);
                double nl = Math.Max(0.0, n[0] * flatLight.L.x + n[1] * flatLight.L.y + n[2] * flatLight.L.z);
                saturates |= flatLight.Ambient.x + flatLight.Direct.x * nl > 1.0;
            }
            Check(f2M2 <= 2e-3 && f2Wmo <= 2e-3 && crossM2 <= 2e-3 && crossWmo <= 2e-3 && saturates,
                  "outdoor models T2: the flat arms against the twin (base x sat(Ambient + D), K = 0, its saturate taken on red) -- the doodads' " +
                  "(_WmvWLModels.x) " + F6(f2M2) + ", the world models' (.y) " + F6(f2Wmo) + "; each arm is its own kind's: the world models' arm " +
                  "leaves a doodad on the hemisphere class (" + F6(crossM2) + ") and the doodads' a world model (" + F6(crossWmo) + ")", log);

            // ---- T3: a tint written as WmvMaterialAnimator writes it (Material.SetColor): the shader reads the stored value,
            // linearised in this linear-space project (the client uses it as authored; kept as the Models viewer has it), and the
            // copy carries it.
            var authored = new Color(0.6f, 0.6f, 0.6f, 1f);
            Material tinted = ModelProbeM2(m2Shader, tex, "WmvWorldLit probe (M2, tinted)", made);
            tinted.SetColor("_Color", authored);
            Material tintedLit = ModelProbeCopy(tinted, m2Variant, made);
            double stored = QualitySettings.activeColorSpace == ColorSpace.Linear ? Mathf.GammaToLinearSpace(authored.r) : authored.r;
            float[] t3 = ModelProbe(tintedLit, quad, light, none);
            double eStored = ModelProbeWorst(t3, c => light.Twin(false, Scaled(albedoAt(c), stored), ModelProbeNormal(n0, n1, c), false));
            double eAuthored = ModelProbeWorst(t3, c => light.Twin(false, Scaled(albedoAt(c), authored.r), ModelProbeNormal(n0, n1, c), false));
            Check(eStored <= 2e-3 && (stored == authored.r || eAuthored > 2e-2) && WmvWorldLitMaterials.Tinted(tintedLit),
                  "outdoor models T3: a tint of " + authored.r.ToString("0.###") + " written through SetColor -- the variant multiplies the base by " +
                  "the stored value the shader reads, " + stored.ToString("0.####") + " (" + QualitySettings.activeColorSpace + " colour space), " +
                  "then lights it: " + F6(eStored) + " against the twin on that value (against the authored value: " + F6(eAuthored) + "); the copy " +
                  "carries it and counts as tinted", log);

            // ---- T4: unlit (lighting mode 0) under the light -- exactly the base: the original's own unlit render (the M2's
            // _Emissive and the world model's _WmoLightBypass, light x 1 under the shipped rig), the M2's with _WmvRig 1 too
            Material m2Unlit = ModelProbeM2(m2Shader, tex, "WmvWorldLit probe (M2, unlit)", made);
            m2Unlit.SetFloat("_Emissive", 1f);
            Material wmoUnlit = ModelProbeWmo(wmoShader, tex, "WmvWorldLit probe (WMO, F_UNLIT)", made);
            wmoUnlit.SetFloat("_WmoLightBypass", 1f);
            Material m2UnlitLit = ModelProbeCopy(m2Unlit, m2Variant, made), wmoUnlitLit = ModelProbeCopy(wmoUnlit, wmoVariant, made);
            float[] baseM2 = ModelProbe(m2Unlit, quad, null, none), baseWmo = ModelProbe(wmoUnlit, quad, null, none);
            float[] u1 = ModelProbe(m2UnlitLit, quad, light, none), u2 = ModelProbe(m2UnlitLit, quad, light, none, 1f), u3 = ModelProbe(wmoUnlitLit, quad, light, none);
            double eBase = Math.Max(ModelProbeWorst(baseM2, albedoAt), ModelProbeWorst(baseWmo, albedoAt));
            Check(ModelProbeSame(u1, baseM2) == 16 && ModelProbeSame(u2, baseM2) == 16 && ModelProbeSame(u3, baseWmo) == 16 && eBase <= 1e-3,
                  "outdoor models T4: unlit under the light -- the M2 variant with _Emissive 1 (and with _WmvRig 1) and the world model's with " +
                  "_WmoLightBypass 1 draw exactly their base, the original's own unlit render, 16 of 16 pixels each (" + ModelProbeSame(u1, baseM2) + ", " +
                  ModelProbeSame(u2, baseM2) + ", " + ModelProbeSame(u3, baseWmo) + "); no world-model gate factor on that path; the base is the " +
                  "texel within " + F6(eBase), log);

            // ---- T5: alpha-tested foliage -- a texel of alpha 0 is discarded (the composite's clear value stays), the other lit
            Texture2D keyedTex = ModelProbeTexture(new Color32(texel0.r, texel0.g, texel0.b, 0), texel1, made);
            Material m2Key = ModelProbeM2(m2Shader, keyedTex, "WmvWorldLit probe (M2, alpha key)", made);
            string described;
            WmvModelBuilder.ApplyBlendMode(m2Key, M2BlendMode.AlphaKey, false, 0.5f, out described);
            Material wmoKey = ModelProbeWmo(wmoShader, keyedTex, "WmvWorldLit probe (WMO, alpha test)", made);
            wmoKey.SetFloat("_AlphaTest", 1f);
            wmoKey.SetFloat("_Cutoff", 0.5019608f);
            Material m2KeyLit = ModelProbeCopy(m2Key, m2Variant, made), wmoKeyLit = ModelProbeCopy(wmoKey, wmoVariant, made);
            float[] k1 = ModelProbe(m2KeyLit, quad, light, none), k2 = ModelProbe(wmoKeyLit, quad, light, none);
            bool cleared = true;
            for (int row = 0; row < 2; row++)
                for (int c = 0; c < 4; c++)
                    for (int k = 0; k < 4; k++)
                        cleared &= k1[4 * (row * 8 + c) + k] == 0f && k2[4 * (row * 8 + c) + k] == 0f;
            bool alphaK1, alphaK2;
            double e5M2 = ModelProbeWorst(k1, c => c < 4 ? null : light.Twin(false, albedoAt(c), ModelProbeNormal(n0, n1, c), false), out alphaK1);
            double e5Wmo = ModelProbeWorst(k2, c => c < 4 ? null : light.Twin(true, albedoAt(c), ModelProbeNormal(n0, n1, c), false), out alphaK2);
            Check(m2KeyLit.IsKeywordEnabled("_ALPHATEST_ON") && cleared && e5M2 <= 2e-3 && e5Wmo <= 2e-3 && alphaK1 && alphaK2,
                  "outdoor models T5: alpha-tested foliage on the variants (the M2 copy carries _ALPHATEST_ON, the world model's _AlphaTest 1): the " +
                  "texel of alpha 0 is discarded -- its 8 pixels keep the clear value " + (cleared ? "exactly" : "NOT") + " -- and the opaque texel is " +
                  "lit (" + F6(e5M2) + ", " + F6(e5Wmo) + ", alpha 1)", log);

            // ---- T6: the variants' else-branch (_WmvWLOn 0: the composite's pin) against the original's render -- reported; no
            // picture draws it (the variants are bound only while the light is shown, and _WmvWLOn is 1 then)
            float[] o1 = ModelProbe(m2, quad, null, none), v1 = ModelProbe(m2Lit, quad, null, none);
            float[] o2 = ModelProbe(wmo, quad, null, none), v2 = ModelProbe(wmoLit, quad, null, none);
            double w6M2 = ModelProbeDiff(o1, v1), w6Wmo = ModelProbeDiff(o2, v2);
            Check(w6M2 <= 2e-3 && w6Wmo <= 2e-3,
                  "outdoor models T6 (reported, gate 2e-3): with _WmvWLOn 0 the variants draw the preview rig as the originals do -- the M2 " +
                  "variant's worst " + F6(w6M2) + ", " + ModelProbeSame(o1, v1) + " of 16 pixels exactly equal; the world model's worst " + F6(w6Wmo) + ", " +
                  ModelProbeSame(o2, v2) + " of 16 exactly equal (a different compiled program; no picture draws this branch)", log);

            // ---- T7: an original (an interior renderer's material) reads no world-light global: _WmvWLOn 1 changes nothing
            float[] on1 = ModelProbe(m2, quad, light, none), on2 = ModelProbe(wmo, quad, light, none);
            float[] lit1 = ModelProbe(m2Lit, quad, light, none);
            Check(ModelProbeSame(on1, o1) == 16 && ModelProbeSame(on2, o2) == 16 && ModelProbeDiff(lit1, o1) > 1e-2,
                  "outdoor models T7: the originals (interior-interim renderers keep them) draw exactly the same with the probe's _WmvWLOn 1 and " +
                  "the light's globals as with 0 -- " + ModelProbeSame(on1, o1) + " and " + ModelProbeSame(on2, o2) + " of 16 pixels (the variant differs " +
                  "from the rig by up to " + F6(ModelProbeDiff(lit1, o1)) + ")", log);

            // ---- T8: two-sided -- the same quad wound the other way (culling off) is lit with the normal as given, not flipped
            Mesh reversed = ModelProbeQuad(n0, n1, true, made);
            double e8 = Math.Max(ModelProbeWorst(ModelProbe(m2Lit, reversed, light, none), twinOf(false, light, false)),
                                 ModelProbeWorst(ModelProbe(wmoLit, reversed, light, none), twinOf(true, light, false)));
            double flipped = 0.0;
            for (int c = 0; c < 8; c++)
            {
                double[] n = ModelProbeNormal(n0, n1, c);
                double[] a = light.Twin(false, albedoAt(c), n, false), b = light.Twin(false, albedoAt(c), new[] { -n[0], -n[1], -n[2] }, false);
                for (int k = 0; k < 3; k++)
                    flipped = Math.Max(flipped, Math.Abs(a[k] - b[k]));
            }
            Check(e8 <= 2e-3 && flipped > 1e-2,
                  "outdoor models T8: two-sided -- the quad wound the other way is lit with the vertex normal as given, " + F6(e8) + " against the " +
                  "twin (a flipped normal would differ by up to " + F6(flipped) + ")", log);

            // ---- T9: the copy -- every property of the original shader's list, the keywords, the queue and RenderType carried; the
            // shader the variant; no manager's tally moved
            int wm0 = WmvWorldModels.LiveMaterials, dm0 = WmvWorldDoodads.LiveMaterials, wc0 = WmvWorldModels.LiveCopies, dc0 = WmvWorldDoodads.LiveCopies;
            Material rich = ModelProbeM2(m2Shader, tex, "WmvWorldLit probe (M2, builder state)", made);
            rich.SetTexture("_SecondTex", keyedTex);
            rich.SetTextureScale("_MainTex", new Vector2(2f, 3f));
            rich.SetTextureOffset("_MainTex", new Vector2(0.25f, 0.5f));
            rich.SetFloat("_CombinerMode", 1f);
            rich.SetFloat("_Unit1UV", 0f);
            rich.SetFloat("_SecondUnitLobe", 2f);
            rich.SetVector("_UvXf1", new Vector4(0.5f, 0.1f, -0.1f, 0.5f));
            rich.SetColor("_Color", new Color(0.8f, 0.5f, 0.25f, 1f));
            WmvModelBuilder.ApplyBlendMode(rich, M2BlendMode.AlphaKey, false, 0.5f, out described);
            Material modulate = ModelProbeM2(m2Shader, tex, "WmvWorldLit probe (M2, modulate 2x)", made);
            WmvModelBuilder.ApplyBlendMode(modulate, M2BlendMode.Mod2x, true, 0.5f, out described);
            Material richWmo = ModelProbeWmo(wmoShader, tex, "WmvWorldLit probe (WMO, builder state)", made);
            richWmo.SetFloat("_WmoPermutation", 2f);
            richWmo.SetTexture("_WmoTex3", keyedTex);
            richWmo.SetFloat("_WmoUv3", 1f);
            richWmo.SetVector("_WmoLayerMask", new Vector4(1f, 0f, 1f, 1f));
            richWmo.SetFloat("_AlphaTest", 1f);
            richWmo.SetOverrideTag("RenderType", "TransparentCutout");
            richWmo.renderQueue = (int)RenderQueue.AlphaTest;
            var problems = new List<string>();
            int compared = 0;
            foreach (Material o in new[] { rich, modulate, richWmo })
            {
                Material c = ModelProbeCopy(o, o == richWmo ? wmoVariant : m2Variant, made);
                int n;
                string bad = CopyDifferences(o, c, out n);
                compared += n;
                if (bad != null)
                    problems.Add(o.name + ": " + bad);
            }
            bool tallies = WmvWorldModels.LiveMaterials == wm0 && WmvWorldDoodads.LiveMaterials == dm0 && WmvWorldModels.LiveCopies == wc0 &&
                           WmvWorldDoodads.LiveCopies == dc0;
            Check(problems.Count == 0 && tallies && compared > 60,
                  "outdoor models T9: a copy (WmvWorldLitMaterials.Copy) of an alpha-keyed two-unit M2 material, a modulate-2x one and a four-layer " +
                  "alpha-tested world-model one carries every property of the original shader's list (" + compared + " compared: floats, colours, " +
                  "vectors, textures with scale and offset), the keywords, the render queue and RenderType; its shader is the variant, its name " +
                  "the original's plus \" (world)\"; no manager's tally moved" + (problems.Count > 0 ? " -- DIFFERS: " + string.Join("; ", problems.ToArray()) : ""),
                  log);

            // ---- T10: the diagnostic views -- 1 the albedo exactly (the world model's without the gate factor), 2 0.5 + 0.5 x the
            // raw interpolant (its length below 1 reaches the target)
            float[] a1 = ModelProbe(m2Lit, quad, light, new Vector4(0f, 0f, 1f, 0f)), a2 = ModelProbe(wmoLit, quad, light, new Vector4(0f, 0f, 1f, 0f));
            Func<int, double[]> normalView = c =>
            {
                double[] n = ModelProbeNormal(n0, n1, c);
                return new[] { 0.5 + 0.5 * n[0], 0.5 + 0.5 * n[1], 0.5 + 0.5 * n[2] };
            };
            bool alphaN1, alphaN2;
            double eN1 = ModelProbeWorst(ModelProbe(m2Lit, quad, light, new Vector4(0f, 0f, 2f, 0f)), normalView, out alphaN1);
            double eN2 = ModelProbeWorst(ModelProbe(wmoLit, quad, light, new Vector4(0f, 0f, 2f, 0f)), normalView, out alphaN2);
            Check(ModelProbeSame(a1, baseM2) == 16 && ModelProbeSame(a2, baseWmo) == 16 && eN1 <= 2e-3 && eN2 <= 2e-3 && alphaN1 && alphaN2,
                  "outdoor models T10: -wmvWorldLitView -- albedo (_WmvWLModels.z 1) draws exactly the base, " + ModelProbeSame(a1, baseM2) + " and " +
                  ModelProbeSame(a2, baseWmo) + " of 16 pixels (the world model's without the gate factor); normal (2) draws 0.5 + 0.5 x the raw " +
                  "interpolant, " + F6(eN1) + " and " + F6(eN2) + ", alpha 1", log);
        }
        finally
        {
            for (int k = 0; k < ids.Length; k++)
                Shader.SetGlobalVector(ids[k], saved[k]);
            Shader.SetGlobalFloat(WmvWorldLight.OnId, savedOn);
            Shader.SetGlobalFloat(RigId, savedRig);
            foreach (UnityEngine.Object o in made)
                if (o != null)
                    UnityEngine.Object.DestroyImmediate(o);
        }
    }

    static double[] Scaled(double[] a, double s) { return new[] { a[0] * s, a[1] * s, a[2] * s }; }

    /// <summary>What a copy does not carry of its original (null: everything): the shader (the variant), the name, every
    /// property of the original shader's list (`compared` counts them), the keywords, the render queue, RenderType.</summary>
    static string CopyDifferences(Material o, Material c, out int compared)
    {
        var bad = new List<string>();
        compared = 0;
        if (c.shader == null || c.shader.name != o.shader.name + WmvWorldLitMaterials.Suffix)
            bad.Add("shader " + (c.shader != null ? c.shader.name : "none"));
        if (c.name != o.name + WmvWorldLitMaterials.Suffix)
            bad.Add("name " + c.name);
        Shader s = o.shader;
        for (int k = 0; k < s.GetPropertyCount(); k++)
        {
            int id = s.GetPropertyNameId(k);
            string name = s.GetPropertyName(k);
            compared++;
            if (!c.HasProperty(id))
            {
                bad.Add(name + " missing");
                continue;
            }
            switch (s.GetPropertyType(k))
            {
                case ShaderPropertyType.Color:
                {
                    Color a = o.GetColor(id), b = c.GetColor(id);
                    if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a)
                        bad.Add(name);
                    break;
                }
                case ShaderPropertyType.Vector:
                {
                    Vector4 a = o.GetVector(id), b = c.GetVector(id);
                    if (a.x != b.x || a.y != b.y || a.z != b.z || a.w != b.w)
                        bad.Add(name);
                    break;
                }
                case ShaderPropertyType.Texture:
                {
                    Vector2 sa = o.GetTextureScale(id), sb = c.GetTextureScale(id), oa = o.GetTextureOffset(id), ob = c.GetTextureOffset(id);
                    if (o.GetTexture(id) != c.GetTexture(id) || sa.x != sb.x || sa.y != sb.y || oa.x != ob.x || oa.y != ob.y)
                        bad.Add(name);
                    break;
                }
                case ShaderPropertyType.Int:
                    if (o.GetInteger(id) != c.GetInteger(id))
                        bad.Add(name);
                    break;
                default:
                    if (o.GetFloat(id) != c.GetFloat(id))
                        bad.Add(name);
                    break;
            }
        }
        var ko = new List<string>(o.shaderKeywords);
        var kc = new List<string>(c.shaderKeywords);
        ko.Sort(StringComparer.Ordinal);
        kc.Sort(StringComparer.Ordinal);
        if (string.Join(" ", ko.ToArray()) != string.Join(" ", kc.ToArray()))
            bad.Add("keywords [" + string.Join(" ", kc.ToArray()) + "] for [" + string.Join(" ", ko.ToArray()) + "]");
        if (c.renderQueue != o.renderQueue)
            bad.Add("queue " + c.renderQueue + " for " + o.renderQueue);
        if (c.GetTag("RenderType", false) != o.GetTag("RenderType", false))
            bad.Add("RenderType " + c.GetTag("RenderType", false) + " for " + o.GetTag("RenderType", false));
        return bad.Count == 0 ? null : string.Join(", ", bad.ToArray());
    }

    /// <summary>A synthetic world model of two groups drawing the same MOMT entry 0 -- group 0 exterior (MOGP 0x8), group 1
    /// interior (0x2000 in MOGP and MOGI) -- built through the real WmvWorldModels of `v` (its texture not given: drawn white).
    /// The renderer indices of the exterior and the interior group in its object tree come out too.</summary>
    static WmvWorldPrototype ContextPrototype(TestWorldView v, uint id, out int exterior, out int interior)
    {
        WmoRoot root = WmoParser.ParseRoot(WmoSynthetic.ContextRoot(new[] { WmoGroupFlags.Outdoor, WmoGroupFlags.Indoor }, id + 1, id + 50, null),
                                           "context root " + id);
        var groups = new[]
        {
            WmoParser.ParseGroup(WmoSynthetic.ContextGroup(0, WmoGroupFlags.Outdoor, 0), "context exterior", 0),
            WmoParser.ParseGroup(WmoSynthetic.ContextGroup(1, WmoGroupFlags.Indoor, 0), "context interior", 1),
        };
        WmvWorldPrototype p = v.BuildPrototype(id, root, groups, new Dictionary<uint, WmvWmoTexture>());
        exterior = interior = -1;
        Renderer[] all = p.Runtime.Root.GetComponentsInChildren<Renderer>(true);
        for (int k = 0; k < all.Length; k++)
        {
            int gi = Array.IndexOf(p.Runtime.GroupObjects, all[k].gameObject);
            if (gi == 0) exterior = k;
            else if (gi == 1) interior = k;
        }
        return p;
    }

    static WorldModelPlacement ContextPlacement(uint id, uint uniqueId)
    {
        return WorldModelPlan.Build(new[] { new WmoPlacement { NameId = id, UniqueId = uniqueId, Flags = 0xC, ScaleRaw = 1024 } }, PlacementFrame.Tile, null)[0];
    }

    static bool IsWorldLit(Material m)
    {
        return m != null && m.shader != null && m.shader.name.EndsWith(WmvWorldLitMaterials.Suffix, StringComparison.Ordinal);
    }

    /// <summary>T11 and T14's frame binding: a world model's exterior and interior groups (one MOMT entry shared) through the real
    /// WmvWorldModels.BuildPrototype and Place on the self-test's TestWorldView, the test driving RebindWorldLit and
    /// WmvWorldLitMaterials.BindFrame (no frame polls these managers).</summary>
    static void OutdoorModelWorldModelTests(Action<string> log)
    {
        var views = new List<TestWorldView>();
        int materials0 = Materials(), lm0 = WmvWorldModels.LiveMaterials, copies0 = WmvWorldModels.LiveCopies;
        string tallies0 = WorldTallies();
        try
        {
            // ---- T11
            var v = new TestWorldView();
            views.Add(v);
            int ext, intr;
            WmvWorldPrototype p = ContextPrototype(v, 9610, out ext, out intr);
            Material original = p.Runtime.Materials.Length > 0 ? p.Runtime.Materials[0] : null;
            var origin = new WorldOrigin(0.0, 0.0, 0.0);
            WmvWorldInstance a = v.PlaceInstance(p, ContextPlacement(9610, 81), origin, 1);
            int builderMaterials = Materials() - materials0, lmBuilt = WmvWorldModels.LiveMaterials, materialsBuilt = Materials();
            bool contexts = p.GroupContexts != null && p.GroupContexts.Length == 2 && p.GroupContexts[0] == WorldLightContext.Outdoor &&
                            p.GroupContexts[1] == WorldLightContext.InteriorInterim;
            bool unbound = ext >= 0 && intr >= 0 && original != null && a.Renderers.Length == 2 && a.Renderers[ext].sharedMaterial == original &&
                           a.Renderers[intr].sharedMaterial == original && p.LitCopies.Count == 0 && p.LitOriginals == null &&
                           WmvWorldModels.LiveCopies == copies0 && !v.Models.WorldLitBound && v.Models.WorldLitSettled;
            Check(contexts && unbound,
                  "outdoor models T11: a world model whose exterior (MOGP 0x8) and interior (0x2000) groups draw one MOMT entry, built and placed " +
                  "through the real WmvWorldModels -- the groups' contexts OUTDOOR and INTERIOR_INTERIM; unbound, both renderers hold the builder's " +
                  "material, no copy is made and no per-renderer array recorded (" + builderMaterials + " material(s) made by the build)", log);
            var b1 = new WmvWorldLitBind { Lit = true };
            v.Models.RebindWorldLit(true, b1);
            Material copy = ext >= 0 ? a.Renderers[ext].sharedMaterial : null;
            bool bound = IsWorldLit(copy) && copy.shader.name == WmvWorldLitMaterials.WorldModelShaderName + WmvWorldLitMaterials.Suffix &&
                         copy.name == original.name + WmvWorldLitMaterials.Suffix && a.Renderers[intr].sharedMaterial == original && v.Models.WorldLitBound &&
                         v.Models.WorldLitSettled && b1.GroupRenderers == 1 && b1.Placements == 1 && b1.OwnMaterials == 1 && b1.CopiesMade == 1 &&
                         b1.NoVariant == 0 && WmvWorldModels.LiveCopies == copies0 + 1 && WmvWorldModels.LiveMaterials == lmBuilt + 1 &&
                         Materials() == materialsBuilt + 1 && b1.Batches[WmvWorldLitMaterials.BlendOpaque] == 1;
            WmvWorldInstance b = v.PlaceInstance(p, ContextPlacement(9610, 82), origin, 1);
            bool placedBound = b.Renderers.Length == 2 && b.Renderers[ext].sharedMaterial == copy && b.Renderers[intr].sharedMaterial == original &&
                               p.LitCopies.Count == 1 && a.LitBound && b.LitBound;
            Check(bound && placedBound,
                  "outdoor models T11: RebindWorldLit(true) -- the exterior group's renderer holds a copy on \"WMV/Map Object (world)\", the interior " +
                  "one keeps the original (a MOMT entry drawn by both kinds of group), settled; one copy made and counted (world materials +1, " +
                  "copies +1); a second instance placed while bound is bound the same by renderer index -- " + b1.Describe(null), log);
            // The placement's binding is counted and timed apart (PlacedWhileShown), and taken once (WmvWorldLitMaterials.TakePlaced).
            WmvWorldLitBind placed = v.Models.PlacedWhileShown;
            string placedLine = placed != null ? placed.Describe(null) : "";
            int placedCalls = WmvWorldLitMaterials.PlacedCalls(v.Models, null);
            WmvWorldLitBind taken = WmvWorldLitMaterials.TakePlaced(v.Models, null);
            Check(placed != null && placed.Lit && placed.Placed && placed.Calls == 1 && placedCalls == 1 && placed.GroupRenderers == 1 &&
                  placed.Placements == 1 && placed.OwnMaterials == 1 && placed.CopiesMade == 0 && placed.Ms >= 0.0 && taken == placed &&
                  v.Models.PlacedWhileShown == null && WmvWorldLitMaterials.TakePlaced(v.Models, null) == null &&
                  placedLine.Contains("-> the outdoor light (placed while shown: 1 instance(s) bound as they were placed): 1 renderer(s) bound to the " +
                                      "world-lit variants (1 group renderer(s) of 1 placement(s), 0 doodad(s)") &&
                  placedLine.Contains("1 renderer(s) on their own materials (interior-interim)"),
                  "outdoor models T11: the instance placed while bound is counted apart -- 1 group renderer bound, 1 interior one on its own, no copy " +
                  "made, timed -- and taken once (\"" + placedLine + "\")", log);
            var b2 = new WmvWorldLitBind { Lit = false };
            v.Models.RebindWorldLit(false, b2);
            bool back = a.Renderers[ext].sharedMaterial == original && a.Renderers[intr].sharedMaterial == original && b.Renderers[ext].sharedMaterial == original &&
                        b.Renderers[intr].sharedMaterial == original && b2.Unbound == 2 && p.LitCopies.Count == 1 && WmvWorldModels.LiveCopies == copies0 + 1 &&
                        !v.Models.WorldLitBound && v.Models.WorldLitSettled && !a.LitBound && !b.LitBound;
            // A second rebind to the originals finds every instance as asked: nothing set, nothing counted.
            var b2b = new WmvWorldLitBind { Lit = false };
            v.Models.RebindWorldLit(false, b2b);
            back &= b2b.Unbound == 0 && !b2b.Touched;
            // A renderer destroyed meanwhile is skipped.
            UnityEngine.Object.DestroyImmediate(b.Renderers[ext]);
            var b3 = new WmvWorldLitBind { Lit = true };
            v.Models.RebindWorldLit(true, b3);
            bool skipped = v.Models.WorldLitSettled && a.Renderers[ext].sharedMaterial == copy && b3.GroupRenderers == 1 && b3.OwnMaterials == 2 && b3.CopiesMade == 0;
            Check(back && skipped,
                  "outdoor models T11: RebindWorldLit(false) puts both instances' renderers back on the original (2 set back; a second rebind " +
                  "sets none), the copy kept and counted until the prototype goes; a rebind with a destroyed renderer skips it and settles (" +
                  b3.GroupRenderers + " bound, " + b3.OwnMaterials + " interior)", log);
            v.DisposeInstance(a);
            v.DisposeInstance(b);
            v.DisposePrototype(p);
            Check(WorldTallies() == tallies0 && WmvWorldModels.LiveMaterials == lm0 && WmvWorldModels.LiveCopies == copies0 && Materials() == materials0,
                  "outdoor models T11: DisposePrototype destroys the copy with the prototype -- the world's tallies " + WorldTallies() + ", its " +
                  "materials and copies, and the materials alive (" + (Materials() - materials0) + " more than at the start) back to their start", log);

            // ---- T14, the frame's binding: WmvWorldLitMaterials.BindFrame rebinds a manager only when asked otherwise
            var w = new TestWorldView();
            views.Add(w);
            int e2, i2;
            WmvWorldPrototype q = ContextPrototype(w, 9620, out e2, out i2);
            WmvWorldInstance qi = w.PlaceInstance(q, ContextPlacement(9620, 83), origin, 1);
            WmvWorldLitBind f0 = WmvWorldLitMaterials.BindFrame(w.Models, null, false);
            WmvWorldLitBind f1 = WmvWorldLitMaterials.BindFrame(w.Models, null, true);
            WmvWorldLitBind f2 = WmvWorldLitMaterials.BindFrame(w.Models, null, true);
            bool litNow = IsWorldLit(qi.Renderers[e2].sharedMaterial) && !IsWorldLit(qi.Renderers[i2].sharedMaterial);
            WmvWorldLitBind f3 = WmvWorldLitMaterials.BindFrame(w.Models, null, false);
            string line = f3 != null ? f3.Describe("overview") : "";
            Check(f0 == null && f1 != null && f1.Lit && f1.Touched && f1.GroupRenderers == 1 && f2 == null && litNow && f3 != null && f3.Touched &&
                  f3.Unbound == 1 && !IsWorldLit(qi.Renderers[e2].sharedMaterial) && line.Contains("(not shown: overview): 1 renderer(s)") &&
                  WmvWorldLitMaterials.BindFrame(null, null, true) == null,
                  "outdoor models T14: the frame's binding (WmvWorldLitMaterials.BindFrame) -- nothing while the managers are as asked (unbound and " +
                  "not lit; lit twice in a row), a rebind when that changes (to the light: the exterior group bound, the interior not; back: \"" + line +
                  "\")", log);
            w.DisposeInstance(qi);
            w.DisposePrototype(q);

            // ---- T14, the driver (WmvMain's frame binding and its lines, WmvWorldLitDriver): the light shown from the start, so
            // the first bind meets no instance and the placements bind as they come; then the overview, the light again, a
            // placement just before a fault.
            var u = new TestWorldView();
            views.Add(u);
            int e3, i3;
            WmvWorldPrototype r = ContextPrototype(u, 9640, out e3, out i3);
            var lines = new List<string>();
            var driver = new WmvWorldLitDriver(lines.Add);
            const int G = 1000;
            WmvWorldLitBind g0 = driver.Frame(u.Models, null, true, null, G, 16.0);
            WmvWorldInstance r1 = u.PlaceInstance(r, ContextPlacement(9640, 84), origin, 1);      // the frame's update: a copy made
            driver.Frame(u.Models, null, true, null, G + 1, 16.0);
            WmvWorldInstance r2 = u.PlaceInstance(r, ContextPlacement(9640, 85), origin, 1);      // the burst goes on
            driver.Frame(u.Models, null, true, null, G + 2, 21.5);
            int midBurst = lines.Count;
            driver.Frame(u.Models, null, true, null, G + 2, 16.0);                               // a second render of the same frame
            int sameFrame = lines.Count;
            driver.Frame(u.Models, null, true, null, G + 3, 16.0);                               // no new placement: said
            bool litFirst = IsWorldLit(r1.Renderers[e3].sharedMaterial) && IsWorldLit(r2.Renderers[e3].sharedMaterial) &&
                            !IsWorldLit(r1.Renderers[i3].sharedMaterial);
            driver.Frame(u.Models, null, false, "overview", G + 4, 16.0);
            bool unlitOverview = !IsWorldLit(r1.Renderers[e3].sharedMaterial) && !IsWorldLit(r2.Renderers[e3].sharedMaterial);
            driver.Frame(u.Models, null, true, null, G + 5, 16.0);
            WmvWorldInstance r3 = u.PlaceInstance(r, ContextPlacement(9640, 86), origin, 1);      // placed, then the light's frame fails
            driver.Frame(u.Models, null, false, WmvWorldLitMaterials.WhyNotShown(null, true, true, null, false), G + 6, 16.0);
            driver.Frame(u.Models, null, false, "fault", G + 7, 16.0);
            bool unlitFault = !IsWorldLit(r1.Renderers[e3].sharedMaterial) && !IsWorldLit(r2.Renderers[e3].sharedMaterial) &&
                              !IsWorldLit(r3.Renderers[e3].sharedMaterial) && !u.Models.WorldLitBound && u.Models.WorldLitSettled;
            string[] want =
            {
                "world light: first lit frame 21.5 ms (frame 1001, ",
                "world light: world models and doodads -> the outdoor light (placed while shown: 2 instance(s) bound as they were placed): 2 renderer(s) " +
                "bound to the world-lit variants (2 group renderer(s) of 2 placement(s), 0 doodad(s); batches by blend mode: opaque 2, ",
                "world light: world models and doodads -> their own materials (not shown: overview): 2 renderer(s) in ",
                "world light: world models and doodads -> the outdoor light: 2 renderer(s) bound to the world-lit variants (2 group renderer(s) of 2 " +
                "placement(s), 0 doodad(s);",
                "world light: world models and doodads -> the outdoor light (placed while shown: 1 instance(s) bound as they were placed): 1 renderer(s) ",
                "world light: world models and doodads -> their own materials (not shown: fault): 3 renderer(s) in ",
            };
            var wrongLines = new List<string>();
            for (int k = 0; k < want.Length; k++)
                if (k >= lines.Count || !lines[k].StartsWith(want[k], StringComparison.Ordinal))
                    wrongLines.Add(k + ": " + (k < lines.Count ? lines[k] : "(none)"));
            bool placedCounts = lines.Count > 1 && lines[1].Contains("1 material(s) copied now") && lines[1].Contains("2 renderer(s) on their own materials") &&
                                lines.Count > 3 && lines[3].Contains("0 material(s) copied now");
            Check(g0 != null && !g0.Touched && midBurst == 1 && sameFrame == 1 && lines.Count == want.Length && wrongLines.Count == 0 && placedCounts &&
                  litFirst && unlitOverview && unlitFault,
                  "outdoor models T14: the frame's driver -- shown from the start, its first bind meets nothing and says nothing; the instances placed " +
                  "while shown are said once a frame passes without a new one (not by a second render of the same frame), the first lit frame " +
                  "before them; the overview and a fault each put every renderer back and say why, a placement just before the fault said first " +
                  "(" + lines.Count + " line(s)" + (wrongLines.Count > 0 ? "; WRONG: " + string.Join(" | ", wrongLines.ToArray()) : "") + ")", log);
            u.DisposeInstance(r1);
            u.DisposeInstance(r2);
            u.DisposeInstance(r3);
            u.DisposePrototype(r);
        }
        finally
        {
            foreach (TestWorldView v in views)
                v.Destroy();
        }
        Check(WorldTallies() == tallies0 && WmvWorldModels.LiveCopies == copies0 && Materials() == materials0,
              "outdoor models: nothing of the world-model binding tests is left (" + WorldTallies() + ", copies " + WmvWorldModels.LiveCopies + ")", log);
    }

    /// <summary>T12: a session's doodads by their contexts -- an MDDF doodad, a MODD record listed by the exterior group and one
    /// listed by the interior group (ContextGroup with MODR) of a placed world model -- bound by the real WmvWorldDoodads; the
    /// load's context line; every tally back after the clear.</summary>
    static void OutdoorModelDoodadTests(Action<string> log)
    {
        var views = new List<TestWorldView>();
        int meshes0 = Meshes(), materials0 = Materials(), textures0 = Resources.FindObjectsOfTypeAll<Texture2D>().Length, models0 = WmvRuntimeModel.Live;
        int wc0 = WmvWorldModels.LiveCopies, dc0 = WmvWorldDoodads.LiveCopies;
        try
        {
            const uint wmo = 9630, group = 9631, wmoTexture = 9650, m2 = 9660, skin = 9661, texture = 9670;
            byte[] root = WmoSynthetic.Concat(WmoSynthetic.ContextRoot(new[] { WmoGroupFlags.Outdoor, WmoGroupFlags.Indoor }, group, wmoTexture, null),
                                              WmoSynthetic.Chunk("MODS", ModsRecord("Set_$DefaultGlobal", 0, 2)),
                                              WmoSynthetic.Chunk("MODI", WmoSynthetic.U32s(m2)),
                                              WmoSynthetic.Chunk("MODD", WmoSynthetic.Concat(ModdRecord(0, 2f, 2f, 0f), ModdRecord(0, 14f, 2f, 0f))));
            int idx = WorldTestTileY * 64 + WorldTestTileX;
            byte[] tileWdt = AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Mphd(0), AdtSynthetic.MainTable(idx),
                                              AdtSynthetic.Maid(idx, TerrainTestRoot, WorldTestObj0));
            byte[] obj0 = AdtSynthetic.Cat(AdtSynthetic.Mver(),
                                           AdtSynthetic.Chunk("MDDF", AdtSynthetic.MddfRecord(m2, 61, 100f, 20f, 300f, 0f, 0f, 0f, 1024, 0x40)),
                                           AdtSynthetic.Chunk("MODF", ModfWithSet(wmo, 62, 0)));
            var files = new WorldFiles();
            files.Add(WmvTerrainSession.RoleWdt, TerrainTestWdt, tileWdt);
            files.Add(WmvTerrainSession.RoleRootAdt, TerrainTestRoot, AdtSynthetic.FlatRoot(WorldTestTileX, WorldTestTileY));
            files.Add(WmvTerrainSession.RoleObj0Adt, WorldTestObj0, obj0);
            files.Add(WmvTerrainSession.RoleWmoRoot, wmo, root);
            files.Add(WmvTerrainSession.RoleWmoGroup, group, WmoSynthetic.ContextGroup(0, WmoGroupFlags.Outdoor, 0, new ushort[] { 0 }));
            files.Add(WmvTerrainSession.RoleWmoGroup, group + 1, WmoSynthetic.ContextGroup(1, WmoGroupFlags.Indoor, 0, new ushort[] { 1 }));
            files.Add(WmvTerrainSession.RoleWmoTexture, wmoTexture, RawBlp(31, 8, 8));
            files.Add(WmvTerrainSession.RoleM2Model, m2, DoodadM2((int)skin, (int)texture));
            files.Add(WmvTerrainSession.RoleM2Skin, skin, M2Synthetic.GeosetSkin(new[] { 0 }));
            files.Add(WmvTerrainSession.RoleM2Texture, texture, RawBlp(37, 8, 8));
            var h = new TerrainHarness();
            var lines = new List<string>();
            h.Log = lines.Add;
            var v = new TestWorldView();
            views.Add(v);
            h.Session.WorldView = v;
            h.Session.Load(DoodadCommand(41, false));
            files.Serve(h, 41);
            WmvIpcClient.TerrainTileReport r = h.For(41);
            WmvWorldScene scene = h.Session.World;
            WmvDoodadInstance mddf = null, outdoor = null, interior = null;
            if (scene != null)
                foreach (WmvDoodadInstance i in scene.Doodads)
                {
                    if (i.Doodad.Source == DoodadSource.Mddf) mddf = i;
                    else if (i.Doodad.ModdIndex == 0) outdoor = i;
                    else if (i.Doodad.ModdIndex == 1) interior = i;
                }
            string contextLine = lines.Find(x => x.Contains("outdoor light contexts")) ?? "";
            bool placed = r != null && r.Status == "built" && r.DoodadPlaced == 3 && mddf != null && outdoor != null && interior != null;
            bool contexts = placed && mddf.Doodad.Context == WorldLightContext.Outdoor && outdoor.Doodad.Context == WorldLightContext.Outdoor &&
                            interior.Doodad.Context == WorldLightContext.InteriorInterim;
            bool line = contextLine.Contains("world models 1 placement(s) (0 incomplete") && contextLine.Contains("2 group(s): 1 exterior, 1 interior") &&
                        contextLine.Contains("doodads 2 outdoor (1 MDDF") && contextLine.Contains("1 listed by exterior groups), 1 interior-interim (0 listed " +
                                                                                                    "by both kinds; 0 listed by no group)");
            Check(placed && contexts && line,
                  "outdoor models T12: a load's doodads take their contexts -- the MDDF doodad and the MODD record the exterior group lists " +
                  "OUTDOOR, the one the interior group lists INTERIOR_INTERIM -- and the load says them (\"" + contextLine + "\"; " +
                  (r != null ? r.Status + ", " + r.DoodadPlaced + " placed" : "no report") + ")", log);
            if (!placed)
                return;
            var bd = new WmvWorldLitBind { Lit = true };
            v.Doodads.RebindWorldLit(true, bd);
            var bm = new WmvWorldLitBind { Lit = true };
            v.Models.RebindWorldLit(true, bm);
            Material own = mddf.Prototype.Materials.Length > 0 ? mddf.Prototype.Materials[0] : null;
            Material lit = mddf.Renderer.sharedMaterial;
            bool doodadsBound = IsWorldLit(lit) && lit.shader.name == WmvWorldLitMaterials.DoodadShaderName + WmvWorldLitMaterials.Suffix &&
                                outdoor.Renderer.sharedMaterial == lit && interior.Renderer.sharedMaterial == own && own != null && bd.Doodads == 2 &&
                                bd.OwnMaterials == 1 && bd.CopiesMade == 1 && WmvWorldDoodads.LiveCopies == dc0 + 1 && bm.GroupRenderers == 1 && bm.OwnMaterials == 1;
            Check(doodadsBound,
                  "outdoor models T12: bound to the light, the MDDF doodad and the exterior group's doodad hold the M2 prototype's copy on \"WMV/Opaque " +
                  "Textured (world)\" (one copy for both), the interior group's doodad the prototype's own material; the world model's exterior group " +
                  "bound, its interior group not -- " + bd.Describe(null), log);
            // Doodads placed while bound (PlaceOn): an outdoor one holds the copy at once, an interior-interim one its own, both counted
            // and timed apart (PlacedWhileShown).
            WorldDoodad m0 = mddf.Doodad;
            var extraOutdoor = new WorldDoodad
            {
                Source = DoodadSource.Mddf, FileDataID = m0.FileDataID, UniqueId = 93, Position = m0.Position, Rotation = m0.Rotation, Scale = 1.0,
                Context = WorldLightContext.Outdoor,
            };
            var extraInterior = new WorldDoodad
            {
                Source = DoodadSource.Mddf, FileDataID = m0.FileDataID, UniqueId = 94, Position = m0.Position, Rotation = m0.Rotation, Scale = 1.0,
                Context = WorldLightContext.InteriorInterim,
            };
            WmvDoodadInstance po = v.PlaceDoodad(mddf.Prototype, extraOutdoor, scene.Origin, 41);
            WmvDoodadInstance pi = v.PlaceDoodad(mddf.Prototype, extraInterior, scene.Origin, 41);
            bool placedOn = po.Renderer.sharedMaterial == lit && pi.Renderer.sharedMaterial == own && po.LitBound && pi.LitBound;
            WmvWorldLitBind pd = WmvWorldLitMaterials.TakePlaced(null, v.Doodads);
            string pdLine = pd != null ? pd.Describe(null) : "";
            Check(placedOn && pd != null && pd.Placed && pd.Calls == 2 && pd.Doodads == 1 && pd.OwnMaterials == 1 && pd.CopiesMade == 0 && pd.Ms >= 0.0 &&
                  v.Doodads.PlacedWhileShown == null && pdLine.Contains("(placed while shown: 2 instance(s) bound as they were placed): 1 renderer(s)"),
                  "outdoor models T12: doodads placed while bound -- the outdoor one holds the prototype's copy, the interior-interim one its own " +
                  "material, both counted apart and timed (\"" + pdLine + "\")", log);
            v.DisposeDoodadInstances(new List<WmvDoodadInstance> { po, pi });
            var ud = new WmvWorldLitBind { Lit = false };
            v.Doodads.RebindWorldLit(false, ud);
            v.Models.RebindWorldLit(false, null);
            var ud2 = new WmvWorldLitBind { Lit = false };
            v.Doodads.RebindWorldLit(false, ud2);
            bool back = mddf.Renderer.sharedMaterial == own && outdoor.Renderer.sharedMaterial == own && interior.Renderer.sharedMaterial == own && ud.Unbound == 2 &&
                        ud2.Unbound == 0 && !mddf.LitBound && !outdoor.LitBound && !interior.LitBound;
            h.Session.Clear("test");
            bool clean = DoodadTallies() == "0/0/0/0/0/0/0" && WorldTallies() == "0/0/0/0/0" && Meshes() == meshes0 && Materials() == materials0 &&
                         Resources.FindObjectsOfTypeAll<Texture2D>().Length == textures0 && WmvRuntimeModel.Live == models0 && WmvWorldDoodads.LiveCopies == dc0 &&
                         WmvWorldModels.LiveCopies == wc0;
            Check(back && clean,
                  "outdoor models T12: back on their own materials when the light goes (2 set back), and the clear takes the copies with their " +
                  "prototypes -- doodads " + DoodadTallies() + ", world " + WorldTallies() + ", materials " + (Materials() - materials0) + " more than at the start", log);
        }
        finally
        {
            foreach (TestWorldView v in views)
                v.Destroy();
        }
    }

    /// <summary>T13 and T14's driver: _WmvWLModels from the switches after a Frame, and ObjectsLit -- false while off, before an
    /// evaluation, without a set (the overview), with the camera not placed, for an invalid light and after a fault; true once
    /// evaluated and valid; the fixtures' pin does not touch it. The driver is put back to the switches' start.</summary>
    static void OutdoorModelDriverTests(Action<string> log)
    {
        Action<string> savedLog = WmvWorldLight.Log;
        Vector4 savedModels = Shader.GetGlobalVector(WmvWorldLight.ModelsId);
        var said = new List<string>();
        try
        {
            WmvWorldLight.Log = said.Add;
            WorldLightSet s = WmvIpcClient.ParseWorldLight(WorldLightLine(""));
            const int F = -5000;
            double x = 1225, y = -3948, zz = 20;
            WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasOn = true, On = false });
            WmvWorldLight.Frame(F, 0.016, s, true, x, y, zz, 0, false);
            bool off = !WmvWorldLight.ObjectsLit;
            WmvWorldLight.SetFromHost(new WmvIpcClient.WorldLightCommand { HasOn = true, On = true, HasClock = true, Clock = "pinned", HasTime = true,
                                                                           Time = "12:00:00" });
            bool before = !WmvWorldLight.ObjectsLit;
            WmvWorldLight.Frame(F + 1, 0.016, s, true, x, y, zz, 0, false);
            bool lit = WmvWorldLight.ObjectsLit;
            // T13: the globals
            WorldLightSwitches sw = WmvWorldLight.Switches;
            var want = new Vector4(sw.DoodadFlat ? 1f : 0f, sw.WorldModelFlat ? 1f : 0f, sw.WorldLitView, 0f);
            Vector4 models = Shader.GetGlobalVector(WmvWorldLight.ModelsId);
            Check(models == want && WmvWorldLight.ModelsVector() == want && (!sw.DrawDefaults || models == Vector4.zero),
                  "outdoor models T13: after a Frame _WmvWLModels is the switches' (" + models + ": the doodads' and the world models' flat arms, " +
                  "-wmvWorldLitView, 0; all 0 by default)", log);
            WmvWorldLight.PinOff();
            bool pinned = WmvWorldLight.ObjectsLit && !WmvWorldLight.PublishedOn;
            WmvWorldLight.Unpin();
            WmvWorldLight.Frame(F + 2, 0.016, null, false, 0, 0, 0, 0, false);
            bool noSet = !WmvWorldLight.ObjectsLit;
            WmvWorldLight.Frame(F + 3, 0.016, s, false, x, y, zz, 0, false);
            bool notPlaced = !WmvWorldLight.ObjectsLit;
            WmvWorldLight.Frame(F + 4, 0.016, s, true, 0, 0, 0, 0, false);       // outside the zone: the default Light 191 has no keys
            bool invalid = !WmvWorldLight.ObjectsLit && WmvWorldLight.Evaluated;
            WmvWorldLight.Frame(F + 5, 0.016, s, true, x, y, zz, 0, false);
            bool again = WmvWorldLight.ObjectsLit;
            WmvWorldLight.FailOff(new InvalidOperationException("self-test: a fault in the light's frame"));
            bool fault = !WmvWorldLight.ObjectsLit;
            var wrong = new List<string>();
            string[] names = { "off", "before an evaluation", "lit", "pinned", "no set", "not placed", "invalid", "lit again", "after the fault" };
            bool[] states = { off, before, lit, pinned, noSet, notPlaced, invalid, again, fault };
            for (int k = 0; k < states.Length; k++)
                if (!states[k])
                    wrong.Add(names[k]);
            Check(wrong.Count == 0,
                  "outdoor models T14: ObjectsLit -- false with the light off, before an evaluation, with no set (the overview, outside the world " +
                  "view), with the camera not placed, for an invalid light and after FailOff; true once evaluated with a valid light; the fixtures' " +
                  "pin sets _WmvWLOn 0 and leaves it true (" + (wrong.Count == 0 ? "each state as expected" : "WRONG: " + string.Join(", ", wrong.ToArray())) + ")",
                  log);
            // T14: why the light is not shown, as the binding line says it (WmvMain passes its frame's camera, light state, view, set
            // and position; no camera after a fault).
            var camObject = new GameObject("_WmvWorldLitWhyCamera");
            camObject.SetActive(false);
            try
            {
                Camera c = camObject.AddComponent<Camera>();
                c.enabled = false;
                c.orthographic = false;
                var whys = new List<string>
                {
                    WmvWorldLitMaterials.WhyNotShown(null, true, true, s, true),
                    WmvWorldLitMaterials.WhyNotShown(c, false, true, s, true),
                    WmvWorldLitMaterials.WhyNotShown(c, true, false, null, false),
                };
                c.orthographic = true;
                whys.Add(WmvWorldLitMaterials.WhyNotShown(c, true, true, null, false));
                c.orthographic = false;
                whys.Add(WmvWorldLitMaterials.WhyNotShown(c, true, true, null, false));
                whys.Add(WmvWorldLitMaterials.WhyNotShown(c, true, true, s, false));
                whys.Add(WmvWorldLitMaterials.WhyNotShown(c, true, true, s, true));
                string got = string.Join(", ", whys.ToArray());
                Check(got == "fault, light off, not the world view, overview, no valid light, camera not placed, no valid light",
                      "outdoor models T14: why the light is not shown -- no camera (a fault), the light off, outside the terrain view, the top-down " +
                      "overview, no scene light, the camera not placed, an invalid light (" + got + ")", log);
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(camObject);
            }
        }
        finally
        {
            WmvWorldLight.ApplySwitches();
            WmvWorldLight.Off();
            WmvWorldLight.Log = savedLog;
            Shader.SetGlobalVector(WmvWorldLight.ModelsId, savedModels);
        }
    }

    /// <summary>WMV_LIGHT=off: a world model and a doodad built and placed by managers of the test's own, which are asked to bind
    /// to the light -- no variant is ever loaded, no copy made, no context computed, every renderer keeps the builder's material,
    /// and nothing is said.</summary>
    static void OutdoorModelHardOffTests(Action<string> log, List<string> said)
    {
        int said0 = said.Count, materials0 = Materials(), meshes0 = Meshes();
        string world0 = WorldTallies(), doodads0 = DoodadTallies();
        var holder = new GameObject("_WmvWorldLitHardOffTest");
        holder.SetActive(false);
        var view = new TestWorldView();
        var doodads = new WmvWorldDoodads(holder.transform);
        var cache = new WmvDoodadTextures();
        var owned = new List<UnityEngine.Object>();
        try
        {
            int ext, intr;
            WmvWorldPrototype wp = ContextPrototype(view, 9690, out ext, out intr);
            WmvWorldInstance wi = view.PlaceInstance(wp, ContextPlacement(9690, 91), new WorldOrigin(0.0, 0.0, 0.0), 1);
            M2ParsedModel model = M2Parser.Parse(DoodadM2(9692, 9693));
            M2ParsedSkin skin = M2SkinParser.Parse(M2Synthetic.GeosetSkin(new[] { 0 }));
            WmvDoodadPrototype dp = doodads.BuildPrototype(9691, model, skin, new Dictionary<int, BlpImage> { { 0, BlpDecoder.Decode(RawBlp(29, 8, 8)) } },
                                                           new Dictionary<int, uint> { { 0, 9693u } }, cache);
            var doodad = new WorldDoodad
            {
                Source = DoodadSource.Mddf, FileDataID = 9691, UniqueId = 92, Position = new WorldPoint(0.0, 0.0, 0.0),
                Rotation = new double[] { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, Scale = 1.0, Context = WorldLightContext.Outdoor,
            };
            WmvDoodadInstance di = doodads.Place(dp, doodad, new WorldOrigin(0.0, 0.0, 0.0), 1);
            view.Models.RebindWorldLit(true, null);
            doodads.RebindWorldLit(true, null);
            WmvWorldLitBind b = WmvWorldLitMaterials.BindFrame(view.Models, doodads, true);
            bool originals = wi.Renderers.Length == 2 && Array.IndexOf(wp.Runtime.Materials, wi.Renderers[0].sharedMaterial) >= 0 &&
                             Array.IndexOf(wp.Runtime.Materials, wi.Renderers[1].sharedMaterial) >= 0 && di.Renderer.sharedMaterial == dp.Materials[0];
            Check(WmvWorldLitMaterials.VariantLoads == 0 && b == null && wp.GroupContexts == null && wp.LitOriginals == null && wp.LitCopies.Count == 0 &&
                  dp.LitCopies.Count == 0 && dp.WorldLit == null && WmvWorldModels.LiveCopies == 0 && WmvWorldDoodads.LiveCopies == 0 &&
                  !view.Models.WorldLitBound && !doodads.WorldLitBound && originals && said.Count == said0,
                  "outdoor models: WMV_LIGHT=off -- a world model and a doodad built, placed and asked to bind: no world-lit variant loaded (" +
                  WmvWorldLitMaterials.VariantLoads + "), no copy, no context, every renderer on the builder's material, nothing said", log);
            owned.Add(di.Root);
            if (dp.Runtime != null)
            {
                owned.Add(dp.Runtime.Root);
                owned.Add(dp.Runtime.Mesh);
                owned.AddRange(dp.Runtime.Materials);
            }
            owned.AddRange(cache.All);
            doodads.DisposeInstance(di);
            doodads.DisposePrototype(dp, cache);
            cache.Dispose();
            view.DisposeInstance(wi);
            view.DisposePrototype(wp);
        }
        finally
        {
            foreach (UnityEngine.Object o in owned)
                if (o != null)
                    UnityEngine.Object.DestroyImmediate(o);
            view.Destroy();
            UnityEngine.Object.DestroyImmediate(holder);
        }
        Check(WorldTallies() == world0 && DoodadTallies() == doodads0 && Materials() == materials0 && Meshes() == meshes0,
              "outdoor models: WMV_LIGHT=off -- and nothing of that test is left (world " + WorldTallies() + ", doodads " + DoodadTallies() + ")", log);
    }
}
