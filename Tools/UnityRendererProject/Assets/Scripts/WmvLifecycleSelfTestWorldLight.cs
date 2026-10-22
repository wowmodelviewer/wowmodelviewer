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
// With WMV_LIGHT=off only the hard off is checked (nothing installed, nothing set, nothing said, no normal stream). The
// driver's state is put back to the switches' start afterwards.

using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEngine;
using UnityEngine.Experimental.Rendering;
using UnityEngine.Rendering;
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
}
