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
// STAGE 10 STEP 8 (docs/world-viewer/10-light-sky-fog.md 6.11): unless WMV_LIGHT=off (then all of the above runs first in the
// main camera's frame, exactly as Stage 09), WmvMain samples the water once a frame AFTER the outdoor light's frame
// (WmvWorldLight.Frame, and on its fault path too): while the outdoor light is shown for that camera, the scene has its
// worldLight object and -wmvWaterLight is clock (the default), the values come from the light's day at the half-minute the
// outdoor light used this frame (Wow/World/LiquidWaterDay.cs, cached: rebuilt only when the lights, the object, the half-minute
// or the day's clamp arm change) -- Stage 09's selection, validity, alphas and point order, the same globals; otherwise from the
// host's noon rows, as Stage 09. The ease snaps when the outdoor light's clock jumped this frame ("clock") and when the source
// changes between the noon rows and the clock ("source"). One log line when the source changes ("liquid light: on the light's
// clock (t hh:mm)" / "liquid light: Stage 09 noon") and one per lights and object when some row light is kept at noon there.
// FrameAfterLight reads the outdoor light's state for it (WmvWorldLight: WaterOnClock, the clock's half-minute, the day's clamp
// arm, ClockJumpedThisFrame), so WmvMain and the self-test take the same path. The outdoor light's fault path asks for the noon
// rows in the same frame: a frame already stepped on the clock (or whose clock step threw) is then taken again on the noon rows
// with 0 s (the source's snap gives their values at once), so the faulting frame's water is Stage 09's as the rest of it is.

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
    static readonly LiquidWaterDay waterDay = new LiquidWaterDay();   // Stage 10 step 8: the values on the light's clock
    static int lastFrame = int.MinValue;
    static int clockFrame = int.MinValue;    // Stage 10 step 8: the frame last stepped (or tried) on the light's clock
    static string lastLine;
    static bool on;
    // What the last line said (Sample makes a line only when one of them changes).
    static object saidLights;
    static bool saidValid;
    static int saidZone = int.MinValue, saidLight = int.MinValue;
    // Stage 10 step 8: the source last said (the clock or not), and the lights and object a noon fall-back was said for.
    static bool saidClock;
    static object fallbackSaidLights, fallbackSaidSet;

    /// <summary>The eased state (the self-test's).</summary>
    public static LiquidLightEase Ease { get { return ease; } }
    /// <summary>_WmvZoneLight as last set.</summary>
    public static bool On { get { return on; } }
    /// <summary>The last log line's text (the self-test's), null before any.</summary>
    public static string LastLine { get { return lastLine; } }
    /// <summary>Stage 10 step 8: the values on the light's clock (the self-test's), and whether the last sample read them.</summary>
    public static LiquidWaterDay WaterDay { get { return waterDay; } }
    public static bool OnClock { get { return ease.OnClock; } }

    /// <summary>One rendered frame (once per `frame`, Time.frameCount): `lights` (null: no scene, or no lights) at game point
    /// (x north, y west, z up), `dt` seconds after the last frame, the liquid clock pinned or not.</summary>
    public static void Frame(int frame, double dt, LiquidLights lights, double x, double y, double z, bool pinned)
    {
        if (frame == lastFrame)
            return;
        lastFrame = frame;
        Sample(lights, x, y, z, dt, pinned);
    }

    /// <summary>Stage 10 step 8: one rendered frame (once per `frame`) whose values come from the outdoor light's day when
    /// `clockSet` is given (the scene's worldLight object while the light is shown for this camera and -wmvWaterLight is clock):
    /// at half-minute `halfMinute` (the outdoor light's this frame), the day clamped at its ends or not (`clamp`, the
    /// -wmvLightWrap arm), `clockJumped` the outdoor light's clock jumped this frame. A null `clockSet`: Stage 09's noon rows.
    /// The frame is taken once, except that a noon request (`clockSet` null) in a frame already stepped on the clock -- or whose
    /// clock step threw -- takes it again on the noon rows with 0 s (the outdoor light's fault path: the light is off now, and
    /// the source's snap gives the noon values at once).</summary>
    public static void Frame(int frame, double dt, LiquidLights lights, double x, double y, double z, bool pinned, WorldLightSet clockSet, int halfMinute,
                             bool clamp, bool clockJumped)
    {
        if (frame == lastFrame && (clockSet != null || clockFrame != frame))
            return;
        bool again = frame == lastFrame;
        lastFrame = frame;
        clockFrame = clockSet != null ? frame : int.MinValue;
        Sample(lights, x, y, z, again ? 0.0 : dt, pinned, clockSet, halfMinute, clamp, clockJumped);
    }

    /// <summary>Stage 10 step 8: the frame after the outdoor light's (WmvMain.WorldLightFrame, after WmvWorldLight.Frame, and on
    /// its fault path with a null `set`): Frame on the light's clock -- the scene's worldLight object `set` at the half-minute the
    /// outdoor light used this frame, its day's clamp arm and its clock jump -- while the outdoor light is shown and
    /// -wmvWaterLight is clock (WmvWorldLight.WaterOnClock); otherwise on Stage 09's noon rows.</summary>
    public static void FrameAfterLight(int frame, double dt, LiquidLights lights, double x, double y, double z, bool pinned, WorldLightSet set)
    {
        WorldLightSet clockSet = set != null && WmvWorldLight.WaterOnClock ? set : null;
        Frame(frame, dt, lights, x, y, z, pinned, clockSet, WmvWorldLight.Clock.HalfMinute, WmvWorldLight.Switches.Rules.Clamp,
              WmvWorldLight.ClockJumpedThisFrame);
    }

    /// <summary>One step, whatever the frame (Frame's body; the self-test calls it directly).</summary>
    public static void Sample(LiquidLights lights, double x, double y, double z, double dt, bool pinned)
    {
        Sample(lights, x, y, z, dt, pinned, null, 0, false, false);
    }

    /// <summary>Stage 10 step 8: one step on the light's clock (`clockSet` not null; see Frame) or on the noon rows.</summary>
    public static void Sample(LiquidLights lights, double x, double y, double z, double dt, bool pinned, WorldLightSet clockSet, int halfMinute, bool clamp,
                              bool clockJumped)
    {
        if (lights == null)
        {
            Off();
            return;
        }
        bool onClock = clockSet != null;
        if (onClock)
        {
            waterDay.Update(lights, clockSet, halfMinute, clamp);
            waterDay.Evaluate(x, y, z, sample);
        }
        else
            lights.Evaluate(x, y, z, sample);
        ease.Step(sample, lights, x, y, z, dt, pinned, onClock && clockJumped, onClock);
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
        if (onClock != saidClock)
        {
            saidClock = onClock;
            Say(onClock ? "liquid light: on the light's clock (t " + LiquidWaterDay.Clock(waterDay.HalfMinute) + ")" : "liquid light: Stage 09 noon");
        }
        if (onClock && waterDay.NoonFallbacks > 0 && (!ReferenceEquals(lights, fallbackSaidLights) || !ReferenceEquals(clockSet, fallbackSaidSet)))
        {
            fallbackSaidLights = lights;
            fallbackSaidSet = clockSet;
            Say("liquid light: " + waterDay.NoonFallbacks + " light(s) keep Stage 09's noon values on the light's clock (their day is not in the " +
                "worldLight object, its slot-0 LightParams is not the row's, or the object lacks a water colour column): " + waterDay.Summary());
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
        saidClock = false;
        fallbackSaidLights = fallbackSaidSet = null;
        waterDay.Reset();
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
