// WmvLiquidLight.cs
//
// THE ZONE LIGHT OF THE LIQUIDS (Stage 09 tuning): once per rendered frame, the scene's zone lights (the host's "lights",
// Wow/World/LiquidLights.cs; WmvTerrainSession.SceneLights) evaluated at the main camera's game position, eased
// (LiquidLightEase) and handed to Resources/WmvLiquid.shader as GLOBALS -- never material properties, so every liquid
// material of every scene sees the same light without being touched:
//   _WmvZoneLight    1: the zone path for water and ocean (the colours below); 0: every liquid in its type colours (the
//                    fallback) -- no scene, no lights, or a base light without water colours (at once, no ease)
//   _WmvRiverClose, _WmvRiverFar, _WmvOceanClose, _WmvOceanFar   rgb, authored 0..1 (the stored bytes / 255)
//   _WmvWaterAlpha, _WmvOceanAlpha                               (shallow, deep)
//   _WmvReflectSky   the High look's sky for a reflection that misses the scene (rgb, authored): the zone light's sky
//                    when it gives one, else the neutral sky (WmvLiquidMaterials.NeutralSky, TUNABLE) -- also whenever the
//                    zone light is off
// WmvMain calls Frame from the main camera's beginCameraRendering (after every camera of the frame has moved); a capture
// that renders the camera again in the same frame takes no second step (once per Time.frameCount). The session turns the
// zone light off when its scene goes (Off). One log line per change of the base light.

using System;
using UnityEngine;
using Wmv.Wow.World;

public static class WmvLiquidLight
{
    public static readonly int ZoneLightId = Shader.PropertyToID("_WmvZoneLight"), RiverCloseId = Shader.PropertyToID("_WmvRiverClose"),
                               RiverFarId = Shader.PropertyToID("_WmvRiverFar"), OceanCloseId = Shader.PropertyToID("_WmvOceanClose"),
                               OceanFarId = Shader.PropertyToID("_WmvOceanFar"), WaterAlphaId = Shader.PropertyToID("_WmvWaterAlpha"),
                               OceanAlphaId = Shader.PropertyToID("_WmvOceanAlpha");

    /// <summary>Where the log lines go (WmvMain sets it; null: nowhere).</summary>
    public static Action<string> Log;

    static readonly LiquidLightEase ease = new LiquidLightEase();
    static readonly LiquidLightSample sample = new LiquidLightSample();
    static int lastFrame = int.MinValue;
    static string lastLine;
    static bool on;
    // What the last line said (Sample makes a line only when one of them changes).
    static object saidLights;
    static bool saidValid;
    static int saidZone = int.MinValue, saidLight = int.MinValue;

    /// <summary>The eased state (the self-test's).</summary>
    public static LiquidLightEase Ease { get { return ease; } }
    /// <summary>_WmvZoneLight as last set.</summary>
    public static bool On { get { return on; } }
    /// <summary>The last log line's text (the self-test's), null before any.</summary>
    public static string LastLine { get { return lastLine; } }

    /// <summary>One rendered frame (once per `frame`, Time.frameCount): `lights` (null: no scene, or no lights) at game point
    /// (x north, y west, z up), `dt` seconds after the last frame, the liquid clock pinned or not.</summary>
    public static void Frame(int frame, double dt, LiquidLights lights, double x, double y, double z, bool pinned)
    {
        if (frame == lastFrame)
            return;
        lastFrame = frame;
        Sample(lights, x, y, z, dt, pinned);
    }

    /// <summary>One step, whatever the frame (Frame's body; the self-test calls it directly).</summary>
    public static void Sample(LiquidLights lights, double x, double y, double z, double dt, bool pinned)
    {
        if (lights == null)
        {
            Off();
            return;
        }
        lights.Evaluate(x, y, z, sample);
        ease.Step(sample, lights, x, y, z, dt, pinned);
        // The log: only when the base light (or the lights object, or the validity) changed -- no text is made otherwise.
        if (!ReferenceEquals(lights, saidLights) || sample.Valid != saidValid || sample.Zone != saidZone || sample.Light != saidLight)
        {
            saidLights = lights;
            saidValid = sample.Valid;
            saidZone = sample.Zone;
            saidLight = sample.Light;
            string from = sample.Zone > 0 ? "zone " + sample.Zone : "map default";
            Say(sample.Valid
                    ? string.Format(System.Globalization.CultureInfo.InvariantCulture, "liquid light: {0} light {1} params {2} (map {3})", from,
                                    sample.Light, sample.LightParams, lights.Map)
                    : string.Format(System.Globalization.CultureInfo.InvariantCulture, "liquid light: none -- {0}{1} (map {2}): the type colours",
                                    sample.Why, sample.Light > 0 ? " (" + from + " light " + sample.Light + ")" : "", lights.Map));
        }
        if (!ease.Valid)
        {
            SetOn(false);
            return;
        }
        double[] v = ease.V;
        Shader.SetGlobalVector(RiverCloseId, Rgb(v, LiquidLightSample.RiverClose));
        Shader.SetGlobalVector(RiverFarId, Rgb(v, LiquidLightSample.RiverFar));
        Shader.SetGlobalVector(OceanCloseId, Rgb(v, LiquidLightSample.OceanClose));
        Shader.SetGlobalVector(OceanFarId, Rgb(v, LiquidLightSample.OceanFar));
        Shader.SetGlobalVector(WaterAlphaId, new Vector4((float)v[LiquidLightSample.WaterAlpha], (float)v[LiquidLightSample.WaterAlpha + 1], 0f, 0f));
        Shader.SetGlobalVector(OceanAlphaId, new Vector4((float)v[LiquidLightSample.OceanAlpha], (float)v[LiquidLightSample.OceanAlpha + 1], 0f, 0f));
        Shader.SetGlobalVector(WmvLiquidMaterials.ReflectSkyId, ease.HasSky ? Rgb(v, LiquidLightSample.Sky) : WmvLiquidMaterials.NeutralSky);
        SetOn(true);
    }

    /// <summary>No scene, or its lights went: _WmvZoneLight 0 now, and the next sample snaps.</summary>
    public static void Off()
    {
        ease.Reset();
        SetOn(false);
        saidLights = null;
        saidZone = saidLight = int.MinValue;
        if (lastLine != null)
            Say("liquid light: none -- no scene with zone lights: the type colours");
    }

    static Vector4 Rgb(double[] v, int at)
    {
        return new Vector4((float)v[at], (float)v[at + 1], (float)v[at + 2], 1f);
    }

    static void SetOn(bool value)
    {
        // (Set every time: a global is cheap, and the value stays right whatever else set it.)
        on = value;
        Shader.SetGlobalFloat(ZoneLightId, value ? 1f : 0f);
        if (!value)
            Shader.SetGlobalVector(WmvLiquidMaterials.ReflectSkyId, WmvLiquidMaterials.NeutralSky);
    }

    static void Say(string line)
    {
        if (line == lastLine)
            return;
        lastLine = line;
        if (Log != null)
            Log(line);
    }
}
