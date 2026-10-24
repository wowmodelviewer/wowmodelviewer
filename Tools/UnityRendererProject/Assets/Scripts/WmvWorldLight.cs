// WmvWorldLight.cs
//
// THE OUTDOOR LIGHT'S FRAME STATE (Stage 10, docs/world-viewer/10-light-sky-fog.md 6.5): once per rendered frame, the scene's
// outdoor light (its load's "worldLight", Wow/World/WorldLight.cs; WmvTerrainSession.SceneWorldLight) evaluated at the main
// camera's game position on the world clock (WorldClock) and eased (WorldLightEase, Wow/World/WorldLightState.cs), then
// published as GLOBALS for the main camera in the world view only. Read by the sky (step 4: WmvWorldSky, Resources/WmvSky.shader),
// the terrain (step 5: Resources/WmvTerrainTextured.shader's world-lit branch), and the exterior world-model groups and outdoor
// doodads (step 6: Resources/WmvWorldLight.hlsl in the world-lit variants WmvOpaqueWorld.shader and WmvWmoWorld.shader, which
// WmvWorldLitMaterials binds while ObjectsLit); the same three end with the client's per-pixel fog (step 7: Resources/
// WmvWorldFog.hlsl, its record packed here once a frame by Wow/World/WorldFog.cs); the water reads nothing of it yet.
// RenderSettings, the preview rig and the shadow rig are never touched. The globals (colours authored 0..1 = the table's byte /
// 255, w 1; directions in Unity's world frame, w 0):
//   _WmvWLOn               1 while the main camera of the world view renders with a valid light; 0 at every other camera's
//                          begin, at every camera's end, in WmvTerrainComposite's renders, in the self-test's fixtures, with no
//                          scene, a scene without an outdoor light, an invalid light (its base light has no keys), in the
//                          top-down overview (orthographic), while switched off -- and never set at all with WMV_LIGHT=off.
//                          Written only when it changes, so outside the world view the per-camera handlers write nothing
//   _WmvWLTerrain          (the terrain lit: 1, or 0 with -wmvTerrainLight=off; the flat variant: 1 with -wmvTerrainVariant=flat;
//                          the fog drawn: 1 -- step 7; 0)
//   _WmvWLModels           (the doodads' flat arm: 1 with -wmvDoodadVariant=flat; the world models' flat arm: 1 with
//                          -wmvWorldModelVariant=flat; the DIAGNOSTIC view of -wmvWorldLitView: 0 none, 1 the albedo, 2 the raw
//                          normal; the fog drawn: 1, step 7). "The fog drawn": this frame's record is not off -- the light
//                          object's product takes a fog mode (WorldFog.ModeFor: wow and wowt the Retail pattern; every other
//                          product off, unless -wmvFogClassic names an arm), -wmvFog is not off and the object's layout has every
//                          column the record is made of (WorldFog.AbsentColumns)
//   _WmvWLFogR0 .. _WmvWLFogR13   (step 7, only while the fog is drawn) the 14 rows of the client's fog record, layer 0
//                          (WorldFog.Pack, once a frame in Evaluate), R2.w the height plane's offset from the eye published
//                          with them (WorldFog.PlaneOffset)
//   _WmvWLEye              (step 7, only while the fog is drawn) the main camera's Unity position for this render (SetEye),
//                          w 1: the fog position is camera-relative
//   _WmvWLLightDir         towards the directional light (WorldLightModel: from the north-west, 37/20 deg by the hour -- the
//                          public table, MEASURED within 2.5 deg at three times in step 1); it does not follow the sun
//   _WmvWLSunDir           towards the sun (WorldLightModel's sun path, INFERRED (public); below the horizon at night)
//   _WmvWLAmbient, _WmvWLHorizonAmbient, _WmvWLGroundAmbient   AmbientColor, HorizonAmbientColor, GroundAmbientColor (the
//                          up, horizon and ground colours of the client's hemisphere variant: MEASURED, MEASURED, INFERRED
//                          (public); the columns PROVEN_BYTES)
//   _WmvWLDirect, _WmvWLSunColor                               DirectColor (MEASURED as the direct light), SunColor
//   _WmvWLSkyTop, _WmvWLSkyMiddle, _WmvWLSkyBand1, _WmvWLSkyBand2, _WmvWLSkySmog, _WmvWLSkyFog   the six sky colours
//   _WmvWLFogA             (FogEnd, FogScaler, FogDensity, FogZScalar)
//   _WmvWLFogB             (FogHeight, FogHeightScaler, FogHeightDensity, SunFogAngle)
//   _WmvWLFogC             (MainFogStartDist, MainFogEndDist, EndFogColorDistance, FogStartOffset)
//   _WmvWLFogD             (SunFogStrength, 0, 0, 0)
//   _WmvWLFogHeightCoef, _WmvWLMainFogCoef, _WmvWLHeightDensityCoef   the three 4-float coefficient columns
//   _WmvWLEndFogColor, _WmvWLSunFogColor, _WmvWLFogHeightColor, _WmvWLEndFogHeightColor   the fog colours
//   _WmvWLFrame            (game far clip in yards -- -wmvGameFarclip, 7000 by default --, the game Z the floating origin
//                          (Unity y 0) stands for, the half-minute, the time of day in seconds)
// The colours and floats are the client's own (PROVEN_BYTES per column), the host's rounding per channel at the half-minute
// (WorldLightDay), the point lights lerped over the base light (Stage 09's rule, INFERRED), eased over frames (snapped when
// -wmvAnimTime pins the clocks, on jumps over 60 yd and on clock jumps, as the water's zone light).
//
// WHEN: WmvMain calls Frame, then Sky, from the main camera's beginCameraRendering (CullWorldModels, after the world models'
// and doodads' culling): the first Frame of a frame advances the clock and evaluates, every Frame publishes, every Sky holds
// or puts back bloom and enqueues the sky for that render (a capture that renders the main camera again in the same frame
// takes no second step). Its own begin and end handlers (installed by Configure, removed by Release -- by the owner only, as
// WmvWaterGrabPass: the self-test runs a second WmvMain) set _WmvWLOn 0 for every other camera and after every camera. A fault
// in it switches the light off (FailOff) and never costs the frame's culling.
// THE SWITCHES (WorldLightSwitches, read once): WMV_LIGHT=off -- hard off, nothing installed, evaluated, set or logged (Stage 09
// exactly); WMV_LIGHT=on or -wmvWorldLight=on -- on from the start (the default is off until the owner accepts the stage, D14);
// -wmvGameTime=HH:MM[:SS] (pinned; 12:00:00 by default), -wmvGameFarclip=N; the debug arms -wmvLightRule, -wmvLightWrap; the
// sky's and terrain's -wmvSky, -wmvSkyTable, -wmvSkyEndFog, -wmvTerrainLight, -wmvTerrainVariant; the world models' and
// doodads' -wmvDoodadLight, -wmvWorldModelLight, -wmvDoodadVariant, -wmvWorldModelVariant, -wmvInteriorLight and the diagnostic
// -wmvWorldLitView (they act where the world's renderers are bound, WmvWorldLitMaterials, except the flat arms and the view,
// which _WmvWLModels carries); the fog's -wmvFog and -wmvFogClassic (step 7). The host switches it, its clock, its time and the
// far clip with setWorldLight (SetFromHost).
// THE LOG: one line per load and per change of the base light (zone, Light, LightParams, time, valid; step 7: this frame's fog
// record or why it is off), one per setWorldLight, one fog census per new light object while the light is on (WorldFog.Census);
// Describe() is the capture log line's field; one "fog record" line each time the published record and eye have held still for
// RecordSettleFrames frames (FogRecordText, the whole record as the GPU gets it, for the rig's row-by-row check, with the main
// camera's anti-aliasing). None of it with WMV_LIGHT=off.

using System;
using System.Collections.Generic;
using System.Globalization;
using UnityEngine;
using UnityEngine.Rendering;
using Wmv.Wow.World;

public static class WmvWorldLight
{
    public static readonly int OnId = Shader.PropertyToID("_WmvWLOn"), LightDirId = Shader.PropertyToID("_WmvWLLightDir"),
                               SunDirId = Shader.PropertyToID("_WmvWLSunDir"), AmbientId = Shader.PropertyToID("_WmvWLAmbient"),
                               HorizonAmbientId = Shader.PropertyToID("_WmvWLHorizonAmbient"),
                               GroundAmbientId = Shader.PropertyToID("_WmvWLGroundAmbient"), DirectId = Shader.PropertyToID("_WmvWLDirect"),
                               SunColorId = Shader.PropertyToID("_WmvWLSunColor"), SkyTopId = Shader.PropertyToID("_WmvWLSkyTop"),
                               SkyMiddleId = Shader.PropertyToID("_WmvWLSkyMiddle"), SkyBand1Id = Shader.PropertyToID("_WmvWLSkyBand1"),
                               SkyBand2Id = Shader.PropertyToID("_WmvWLSkyBand2"), SkySmogId = Shader.PropertyToID("_WmvWLSkySmog"),
                               SkyFogId = Shader.PropertyToID("_WmvWLSkyFog"), FogAId = Shader.PropertyToID("_WmvWLFogA"),
                               FogBId = Shader.PropertyToID("_WmvWLFogB"), FogCId = Shader.PropertyToID("_WmvWLFogC"),
                               FogDId = Shader.PropertyToID("_WmvWLFogD"), FogHeightCoefId = Shader.PropertyToID("_WmvWLFogHeightCoef"),
                               MainFogCoefId = Shader.PropertyToID("_WmvWLMainFogCoef"),
                               HeightDensityCoefId = Shader.PropertyToID("_WmvWLHeightDensityCoef"),
                               EndFogColorId = Shader.PropertyToID("_WmvWLEndFogColor"), SunFogColorId = Shader.PropertyToID("_WmvWLSunFogColor"),
                               FogHeightColorId = Shader.PropertyToID("_WmvWLFogHeightColor"),
                               EndFogHeightColorId = Shader.PropertyToID("_WmvWLEndFogHeightColor"), FrameId = Shader.PropertyToID("_WmvWLFrame"),
                               TerrainId = Shader.PropertyToID("_WmvWLTerrain"), ModelsId = Shader.PropertyToID("_WmvWLModels");
    /// <summary>Stage 10 step 7: the fog record's rows (_WmvWLFogR0 .. _WmvWLFogR13, WorldFogRecord's) and the eye.</summary>
    public static readonly int[] FogRowIds = FogRowNames();
    public static readonly int EyeId = Shader.PropertyToID("_WmvWLEye");

    static int[] FogRowNames()
    {
        var ids = new int[WorldFogRecord.Rows];
        for (int k = 0; k < ids.Length; k++)
            ids[k] = Shader.PropertyToID("_WmvWLFogR" + k.ToString(CultureInfo.InvariantCulture));
        return ids;
    }

    /// <summary>The colour globals in the order Publish sets them, with their column (WorldLightColumns).</summary>
    static readonly int[] ColourIds =
    {
        AmbientId, HorizonAmbientId, GroundAmbientId, DirectId, SunColorId, SkyTopId, SkyMiddleId, SkyBand1Id, SkyBand2Id, SkySmogId, SkyFogId,
        EndFogColorId, SunFogColorId, FogHeightColorId, EndFogHeightColorId,
    };
    static readonly int[] ColourColumns =
    {
        WorldLightColumns.AmbientColor, WorldLightColumns.HorizonAmbientColor, WorldLightColumns.GroundAmbientColor, WorldLightColumns.DirectColor,
        WorldLightColumns.SunColor, WorldLightColumns.SkyTopColor, WorldLightColumns.SkyMiddleColor, WorldLightColumns.SkyBand1Color,
        WorldLightColumns.SkyBand2Color, WorldLightColumns.SkySmogColor, WorldLightColumns.SkyFogColor, WorldLightColumns.EndFogColor,
        WorldLightColumns.SunFogColor, WorldLightColumns.FogHeightColor, WorldLightColumns.EndFogHeightColor,
    };

    /// <summary>Where the log lines go (Configure sets it; null: nowhere).</summary>
    public static Action<string> Log;

    static WorldLightSwitches switches;
    static bool applied;                      // the switches' starting state taken (ApplySwitches)
    static object owner;
    static bool installed;
    static bool on;                           // asked for: the switches, then setWorldLight
    static readonly WorldClock clock = new WorldClock();
    static double farClip = WorldLightModel.DefaultFarClip;
    static readonly WorldLightEvaluator evaluator = new WorldLightEvaluator();
    static readonly WorldLightSample sample = new WorldLightSample();
    static readonly WorldLightEase ease = new WorldLightEase();
    static int lastFrame = int.MinValue;
    static bool evaluated;                    // this frame's state came from an object at the camera (the light on)
    static bool clockJumped;
    static object lastSet;
    static int pinnedOff;
    static bool publishedOn;
    static bool onWritten;                    // _WmvWLOn written at least once (later writes only on a change)
    static double originZ;
    static readonly double[] lightDir = new double[3], sunDir = new double[3];
    static readonly SkyInputs skyInputs = new SkyInputs();
    static bool skyDrawn;                     // the last Sky enqueued the sky (runtimeState's skyOn)
    static string lastLine;
    // What the last state line said (a line is made only when one of them changes).
    static object saidSet;
    static bool saidValid, saidEvaluated;
    static int saidZone = int.MinValue, saidLight = int.MinValue, saidParams = int.MinValue;
    // Stage 10 step 7, the fog: the last set's mode (WorldFog.ModeFor; Off when its layout lacks a column of the record, named
    // in fogAbsent), the set its census was said for, this frame's record (Pack, in Evaluate; Off when nothing is shown), the eye
    // Publish uploads (SetEye) and the sun direction Pack reads (Unity world, the sky's vector).
    static WorldFogMode fogMode;
    static string fogAbsent;
    static object censusSet;
    static readonly WorldFogRecord fogRecord = new WorldFogRecord();
    static Vector3 eye;
    static readonly double[] sunUnity = new double[3];
    // Stage 10 step 7: the settled record's line (log only) -- this frame's published values (56 rows, R2.w as published, the
    // eye, the mode), the last frame's, the frames they have held, and the values last said. No allocation until a line is said.
    const int RecordSettleFrames = 30;
    const int RecordValues = 4 * WorldFogRecord.Rows + 4;
    static readonly double[] recordNow = new double[RecordValues], recordPrev = new double[RecordValues], recordSaid = new double[RecordValues];
    static int recordHeld;
    static bool recordSaidAny;

    /// <summary>The main camera's anti-aliasing, said with each settled fog record (WmvMain sets it after Configure,
    /// WmvMain.AntiAliasingText; null: not said).</summary>
    public static Func<string> FrameDescription;

    /// <summary>The switches (parsed once: the command line, WMV_DEBUG's tokens, WMV_LIGHT).</summary>
    public static WorldLightSwitches Switches
    {
        get
        {
            if (switches == null)
            {
                var args = new List<string>(Environment.GetCommandLineArgs());
                string fromEnv = Environment.GetEnvironmentVariable("WMV_DEBUG");
                if (!string.IsNullOrEmpty(fromEnv))
                    args.AddRange(fromEnv.Split(new[] { ' ', '\t', ',', ';' }, StringSplitOptions.RemoveEmptyEntries));
                switches = WorldLightSwitches.Parse(args, Environment.GetEnvironmentVariable("WMV_LIGHT"));
            }
            return switches;
        }
    }

    /// <summary>WMV_LIGHT=off: nothing is installed, evaluated, set or logged (Stage 09 exactly).</summary>
    public static bool HardOff { get { return Switches.HardOff; } }
    /// <summary>The light is on: asked for (a switch or setWorldLight) and not hard off.</summary>
    public static bool Enabled { get { return on && !HardOff; } }
    /// <summary>The terrain may want the outdoor light's normals: not hard off and not -wmvTerrainLight=off. The mesh builder then
    /// keeps each chunk's MCNR bytes (TerrainChunkMesh.McnrRaw, counted in TerrainTileMesh.McnrBytes) for as long as the tile's
    /// mesh is kept, so a tile built while the light is off can be drawn again with the stream once it is switched on.</summary>
    public static bool TerrainNormalsWanted { get { return !HardOff && Switches.TerrainLight; } }
    /// <summary>The terrain builder gives the chunks drawn textured the outdoor light's normal stream (UV1) now: wanted and the
    /// light on, decided when the tile is built. With the light off -- the default until the host switches it on -- the chunks
    /// carry Stage 09's vertex data exactly. A setWorldLight that switches it on draws the resident tiles built without the
    /// stream again (WmvMain.HandleSetWorldLight, WmvTerrainSession.WorldLightOn). A tile built while the light was on keeps
    /// its stream until it is built again or released, also after the light goes off (nothing reads it then): switching off
    /// costs no rebuild, and switching on again none for that tile.</summary>
    public static bool TerrainNormalsUploaded { get { return TerrainNormalsWanted && Enabled; } }
    public static bool Installed { get { return installed; } }
    /// <summary>Who installed the handlers through Configure (null: nobody).</summary>
    public static object Owner { get { return owner; } }
    /// <summary>_WmvWLOn as last set (the self-test's).</summary>
    public static bool PublishedOn { get { return publishedOn; } }
    public static WorldClock Clock { get { return clock; } }
    public static double FarClip { get { return farClip; } }
    public static WorldLightSample Sample { get { return sample; } }
    public static WorldLightEase Ease { get { return ease; } }
    public static bool Evaluated { get { return evaluated; } }
    /// <summary>The last frame's directions (game frame, towards the light and the sun).</summary>
    public static double[] LightDirection { get { return lightDir; } }
    public static double[] SunDirection { get { return sunDir; } }
    /// <summary>The sky's inputs of the last Sky that drew it, and whether the last Sky drew it.</summary>
    public static SkyInputs SkyState { get { return skyInputs; } }
    public static bool SkyDrawn { get { return skyDrawn; } }
    public static string LastLine { get { return lastLine; } }
    /// <summary>Stage 10 step 7: the last set's fog mode, this frame's record (Mode Off when no fog is drawn) and the eye.</summary>
    public static WorldFogMode FogMode { get { return fogMode; } }
    public static WorldFogRecord FogRecord { get { return fogRecord; } }
    public static Vector3 Eye { get { return eye; } }
    /// <summary>The fog is drawn for the main camera now (_WmvWLTerrain.z and _WmvWLModels.w 1): shown, and this frame's record
    /// not off.</summary>
    public static bool FogDrawn { get { return Showing && fogRecord.Mode != WorldFogMode.Off; } }

    /// <summary>The switches' starting state: on or off, the clock pinned at -wmvGameTime (12:00:00), the far clip; the frame
    /// state forgotten. Configure takes it once; the self-test puts it back after its checks.</summary>
    public static void ApplySwitches()
    {
        WorldLightSwitches w = Switches;
        on = w.On;
        clock.Running = false;
        clock.Seconds = w.TimeSeconds;
        clock.MapOverrideMinutes = -1;
        farClip = w.FarClip;
        applied = true;
        lastLine = null;
        ForgetFrame();
    }

    static void ForgetFrame()
    {
        ease.Reset();
        sample.Reset();
        evaluated = false;
        lastSet = null;
        lastFrame = int.MinValue;
        clockJumped = false;
        skyDrawn = false;
        saidSet = null;
        saidZone = saidLight = saidParams = int.MinValue;
        fogMode = WorldFogMode.Off;     // taken again from the next set (its census is not repeated: censusSet stays)
        fogAbsent = null;
        fogRecord.Clear();
        recordHeld = 0;
        recordSaidAny = false;
    }

    /// <summary>The terrain session's setup (WmvMain): unless WMV_LIGHT=off (then nothing at all, not even a log line; only a
    /// setWorldLight the host sends anyway is answered with its refusal), says what the outdoor light does and installs the
    /// per-camera handlers -- `who` their owner when this call installs them (Release(who) removes them; another caller's does
    /// not). The switches' starting state is taken on the first call.</summary>
    public static void Configure(object who, Action<string> log)
    {
        Log = log;
        if (HardOff)
            return;
        if (!applied)
            ApplySwitches();
        WorldLightSwitches w = Switches;
        if (!installed)
        {
            RenderPipelineManager.beginCameraRendering += OnBeginCamera;
            RenderPipelineManager.endCameraRendering += OnEndCamera;
            installed = true;
            owner = who;
        }
        SetOn(false);
        Say("world light: " + w.Describe() + "; light direction " + WorldLightModel.LightDirectionGrade + ", sun path " + WorldLightModel.SunPathGrade +
            ", game far clip default " + WorldLightModel.DefaultFarClipGrade + " (the reference sessions' setting); read by the sky, the " +
            "terrain, exterior world-model groups and outdoor doodads (steps 4-6) and fogged on the terrain, those groups and doodads (step 7)");
    }

    /// <summary>Stage 10 step 7: the main camera's Unity position for the render about to be drawn (WmvMain.WorldLightFrame, before
    /// Frame): the fog's eye (_WmvWLEye) and, with the floating origin's game Z, the camera height of the fog's height plane
    /// (R2.w), both taken by Publish from the eye it uploads -- so a second render of Camera.main in the same frame at another
    /// pose publishes its own.</summary>
    public static void SetEye(Vector3 position)
    {
        eye = position;
    }

    /// <summary>The terrain session's end (WmvMain.DisposeTerrain): removes the handlers when `who` installed them, and only then
    /// (bloom put back, the sky's mesh and material destroyed, the light object the census was said for let go: a new session
    /// brings new objects). True when it removed them.</summary>
    public static bool Release(object who)
    {
        if (who == null || who != owner)
            return false;
        RenderPipelineManager.beginCameraRendering -= OnBeginCamera;
        RenderPipelineManager.endCameraRendering -= OnEndCamera;
        installed = false;
        owner = null;
        SetOn(false);
        WmvWorldSky.HoldBloom(false, Log);
        WmvWorldSky.DropResources();
        ForgetFrame();
        censusSet = null;
        return true;
    }

    /// <summary>The self-test's fixtures: _WmvWLOn is 0 for every render while pinned (nested; Unpin undoes one PinOff).</summary>
    public static void PinOff()
    {
        pinnedOff++;
        SetOn(false);
    }

    public static void Unpin()
    {
        if (pinnedOff > 0)
            pinnedOff--;
    }

    public static bool PinnedOff { get { return pinnedOff > 0; } }

    /// <summary>The scene went (or never had an outdoor light), or the light was switched off (setWorldLight, FailOff): _WmvWLOn
    /// 0 now, the next sample snaps, and the sky's mesh and material go (made again at the next draw), so after clearTerrain or
    /// a model load runtimeState's liveMeshes and liveMaterials are Stage 09's.</summary>
    public static void Off()
    {
        ease.Reset();
        evaluated = false;
        fogRecord.Clear();
        SetOn(false);
        skyDrawn = false;
        WmvWorldSky.DropResources();
    }

    /// <summary>A fault in the light's frame (WmvMain.WorldLightFrame catches it): the light switched off -- the frame keeps
    /// Stage 09's picture -- and said once per fault. The host or a switch may turn it on again.</summary>
    public static void FailOff(Exception e)
    {
        if (HardOff)
            return;
        on = false;
        Off();
        skyDrawn = false;
        WmvWorldSky.HoldBloom(false, Log);
        string what = e != null ? e.GetType().Name + ": " + e.Message : "unknown";
        Say("world light: switched OFF after a fault in its frame (" + what + ") -- the frame is drawn as Stage 09 draws it");
    }

    /// <summary>One rendered frame of the main camera: `set` (null: no scene, no outdoor light, not the world view) at game point
    /// (x north, y west, z up) when `positioned`, `dt` seconds after the last frame; `originZ` the game Z of Unity's y 0;
    /// `animPinned` -wmvAnimTime pins the clocks (the ease snaps, as the water's). The first call of `frame`
    /// (Time.frameCount) advances the clock and evaluates; every call publishes.</summary>
    public static void Frame(int frame, double dt, WorldLightSet set, bool positioned, double x, double y, double z, double originZ,
                             bool animPinned)
    {
        if (HardOff)
            return;
        if (frame != lastFrame)
        {
            lastFrame = frame;
            clock.Advance(dt);
            Evaluate(set, positioned, x, y, z, originZ, dt, animPinned);
            Publish();
            WatchRecord();
            return;
        }
        Publish();
    }

    /// <summary>Stage 10 step 7 (log only): once a frame, after the frame's first Publish -- when the fog record as published
    /// (R2.w with this render's eye) and the eye have held for RecordSettleFrames frames and differ from the last record said,
    /// says it once ("world light: fog record ..."): the record of a settled view, such as a capture's, for the rig's row-by-row
    /// check. Reads only; nothing while the fog is not drawn.</summary>
    static void WatchRecord()
    {
        if (!Showing || fogRecord.Mode == WorldFogMode.Off)
        {
            recordHeld = 0;
            return;
        }
        double[] r = fogRecord.R;
        for (int k = 0; k < 4 * WorldFogRecord.Rows; k++)
            recordNow[k] = k == 11 ? WorldFog.PlaneOffset(eye.y + originZ, fogRecord) : r[k];
        recordNow[56] = eye.x;
        recordNow[57] = eye.y;
        recordNow[58] = eye.z;
        recordNow[59] = (double)fogRecord.Mode;
        bool same = true;
        for (int k = 0; k < RecordValues && same; k++)
            same = recordNow[k] == recordPrev[k];
        if (!same)
        {
            Array.Copy(recordNow, recordPrev, RecordValues);
            recordHeld = 0;
            return;
        }
        if (++recordHeld != RecordSettleFrames)
            return;
        bool said = recordSaidAny;
        for (int k = 0; k < RecordValues && said; k++)
            said = recordNow[k] == recordSaid[k];
        if (said)
            return;
        Array.Copy(recordNow, recordSaid, RecordValues);
        recordSaidAny = true;
        string frameText = FrameDescription != null ? FrameDescription() : null;
        Say("world light: " + FogRecordText() + " (held " + RecordSettleFrames.ToString(CultureInfo.InvariantCulture) + " frames)" +
            (frameText != null ? "; " + frameText : ""));
    }

    /// <summary>Frame's evaluation (the self-test calls it with a frame number of its own through Frame). Step 7: a new set
    /// takes its fog mode (Off when its layout lacks a column of the record); while the light is on, a set not counted yet says
    /// its fog census once; a shown state packs this frame's fog record (at the camera's game height `z`; Publish puts the eye's
    /// own plane offset in) before the state line.</summary>
    static void Evaluate(WorldLightSet set, bool positioned, double x, double y, double z, double origin, double dt, bool animPinned)
    {
        if (!ReferenceEquals(set, lastSet))
        {
            if (clock.UseMap(set))
                clockJumped = true;
            lastSet = set;
            fogMode = set != null ? WorldFog.ModeFor(set.Product, Switches.Fog, Switches.FogClassic) : WorldFogMode.Off;
            fogAbsent = WorldFog.AbsentColumns(set, fogMode);
            if (fogAbsent != null)
                fogMode = WorldFogMode.Off;     // an absent column would be packed as 0 in every key: no fog rather than a wrong one
        }
        if (Enabled && set != null && !ReferenceEquals(set, censusSet))
            SayCensus(set);
        if (!Enabled || set == null || !positioned)
        {
            if (evaluated)
                ease.Reset();
            evaluated = false;
            sample.Reset();
            fogRecord.Clear();
            SayState(set);
            return;
        }
        evaluator.Evaluate(set, x, y, z, clock.Effective, 0, Switches.Rules, sample);
        ease.Step(sample, set, x, y, z, dt, animPinned, clockJumped);
        clockJumped = false;
        evaluated = true;
        originZ = origin;
        WorldLightModel.LightDirection(clock.Effective, out lightDir[0], out lightDir[1], out lightDir[2]);
        WorldLightModel.SunDirection(clock.Effective, out sunDir[0], out sunDir[1], out sunDir[2]);
        // Stage 10 step 7: this frame's fog record, once a frame, from the eased state (the sun as the sky gets it); off while
        // the state is not valid.
        Vector4 sun = Direction(sunDir);
        sunUnity[0] = sun.x;
        sunUnity[1] = sun.y;
        sunUnity[2] = sun.z;
        WorldFog.Pack(ease.V, farClip, z, clock.HalfMinute, sunUnity, set.MapFlags, ease.Valid ? fogMode : WorldFogMode.Off, fogRecord);
        SayState(set);
    }

    /// <summary>Stage 10 step 7: the fog census of a light object (WorldFog.Census), said once per set while the light is on --
    /// not again for the same set after ForgetFrame, an overview toggle or ApplySwitches.</summary>
    static void SayCensus(WorldLightSet set)
    {
        censusSet = set;
        WorldFogCensus c = WorldFog.Census(set);
        Say("world light: " + c.Line(set.Map, set.Product, fogMode));
    }

    /// <summary>Is there a valid eased state to show for the main camera now (on, evaluated, valid, not pinned off)?</summary>
    static bool Showing { get { return Enabled && evaluated && ease.Valid && pinnedOff == 0; } }

    /// <summary>Stage 10 step 6: the world's renderers are drawn lit now (WmvMain.WorldLightFrame binds them by it,
    /// WmvWorldLitMaterials.BindFrame) -- on, evaluated at the main camera of the perspective world view, the light valid:
    /// Showing without the fixtures' pin (a fixture draws no world prototype, and a rebind per composite would be wasted). False
    /// in the top-down overview and outside the world view (no set is evaluated there), with the camera not placed, without a
    /// valid light, while off, after a fault (FailOff) and with WMV_LIGHT=off.</summary>
    public static bool ObjectsLit { get { return Enabled && evaluated && ease.Valid; } }

    /// <summary>The globals for the camera rendering now (the main camera's begin): every value from the eased state, then
    /// _WmvWLOn 1 -- or only _WmvWLOn 0 when there is nothing valid to show, while pinned off, or switched off.</summary>
    static void Publish()
    {
        if (!Showing)
        {
            SetOn(false);
            return;
        }
        double[] v = ease.V;
        for (int k = 0; k < ColourIds.Length; k++)
        {
            int at = WorldLightSample.ColourAt(ColourColumns[k]);
            Shader.SetGlobalVector(ColourIds[k], new Vector4((float)v[at], (float)v[at + 1], (float)v[at + 2], 1f));
        }
        Shader.SetGlobalVector(LightDirId, Direction(lightDir));
        Shader.SetGlobalVector(SunDirId, Direction(sunDir));
        Shader.SetGlobalVector(FogAId, Floats(v, WorldLightColumns.FogEnd, WorldLightColumns.FogScaler, WorldLightColumns.FogDensity, WorldLightColumns.FogZScalar));
        Shader.SetGlobalVector(FogBId, Floats(v, WorldLightColumns.FogHeight, WorldLightColumns.FogHeightScaler, WorldLightColumns.FogHeightDensity,
                                              WorldLightColumns.SunFogAngle));
        Shader.SetGlobalVector(FogCId, Floats(v, WorldLightColumns.MainFogStartDist, WorldLightColumns.MainFogEndDist, WorldLightColumns.EndFogColorDistance,
                                              WorldLightColumns.FogStartOffset));
        Shader.SetGlobalVector(FogDId, new Vector4((float)v[WorldLightSample.FloatAt(WorldLightColumns.SunFogStrength)], 0f, 0f, 0f));
        int h = WorldLightColumns.FogHeightCoefficients, m = WorldLightColumns.MainFogCoefficients, d = WorldLightColumns.HeightDensityFogCoeff;
        Shader.SetGlobalVector(FogHeightCoefId, Floats(v, h, h + 1, h + 2, h + 3));
        Shader.SetGlobalVector(MainFogCoefId, Floats(v, m, m + 1, m + 2, m + 3));
        Shader.SetGlobalVector(HeightDensityCoefId, Floats(v, d, d + 1, d + 2, d + 3));
        Shader.SetGlobalVector(FrameId, new Vector4((float)farClip, (float)originZ, clock.HalfMinute, (float)clock.Effective));
        bool fog = fogRecord.Mode != WorldFogMode.Off;
        if (fog)
            PublishFog();
        Shader.SetGlobalVector(TerrainId, new Vector4(Switches.TerrainLight ? 1f : 0f, Switches.TerrainFlat ? 1f : 0f, fog ? 1f : 0f, 0f));
        Shader.SetGlobalVector(ModelsId, ModelsVector());
        SetOn(true);
    }

    /// <summary>Stage 10 step 7: this frame's fog record (WorldFog.Pack's 14 rows) and the eye of this render (SetEye), only while
    /// the fog is drawn. R2.w is the height plane's offset for the camera height of the eye uploaded now, eye.y + originZ (Unity
    /// y = game Z - originZ at scale 1, TerrainMesh), so the plane and the fog position share one camera. No allocation.</summary>
    static void PublishFog()
    {
        double[] r = fogRecord.R;
        for (int k = 0; k < WorldFogRecord.Rows; k++)
        {
            int at = 4 * k;
            double w = k == 2 ? WorldFog.PlaneOffset(eye.y + originZ, fogRecord) : r[at + 3];
            Shader.SetGlobalVector(FogRowIds[k], new Vector4((float)r[at], (float)r[at + 1], (float)r[at + 2], (float)w));
        }
        Shader.SetGlobalVector(EyeId, new Vector4(eye.x, eye.y, eye.z, 1f));
    }

    /// <summary>_WmvWLModels from the switches and the fog: (doodads' flat arm, world models' flat arm, -wmvWorldLitView, the fog
    /// drawn -- step 7: 1 while this frame's record is not off).</summary>
    public static Vector4 ModelsVector()
    {
        WorldLightSwitches w = Switches;
        return new Vector4(w.DoodadFlat ? 1f : 0f, w.WorldModelFlat ? 1f : 0f, w.WorldLitView, fogRecord.Mode != WorldFogMode.Off ? 1f : 0f);
    }

    /// <summary>The sky and bloom for this render of the main camera (WmvMain, after Frame): bloom held at 0 while the light is
    /// on in the world view and put back otherwise; the sky enqueued while the state is shown, the sky group valid, -wmvSky not
    /// off and `camera` the perspective world view's (WmvWorldSky.Wants). `worldView`: the terrain view, not the top-down
    /// overview.</summary>
    public static void Sky(Camera camera, bool worldView)
    {
        if (HardOff)
            return;
        WmvWorldSky.HoldBloom(Enabled && worldView, Log);
        skyDrawn = false;
        if (!Showing || !Switches.Sky || (sample.Groups & WorldLightSample.GroupSky) == 0 || !WmvWorldSky.Wants(camera, worldView))
            return;
        SkyDome.Inputs(ease.V, clock.HalfMinute, farClip, Switches.SkyEndFog, skyInputs);
        Vector4 sun = Direction(sunDir);
        skyInputs.Sun[0] = sun.x;
        skyInputs.Sun[1] = sun.y;
        skyInputs.Sun[2] = sun.z;
        skyDrawn = WmvWorldSky.Enqueue(camera, worldView, skyInputs, Switches.SkyTableB, Log);
    }

    /// <summary>A direction of the game's frame as the Unity vector Publish sets (WorldToUnity.Direction, w 0); the self-test
    /// compares the published globals with the pure model through it.</summary>
    public static Vector4 Direction(double[] game)
    {
        float ux, uy, uz;
        WorldToUnity.Direction(game[0], game[1], game[2], out ux, out uy, out uz);
        return new Vector4(ux, uy, uz, 0f);
    }

    static Vector4 Floats(double[] v, int a, int b, int c, int d)
    {
        return new Vector4((float)v[WorldLightSample.FloatAt(a)], (float)v[WorldLightSample.FloatAt(b)], (float)v[WorldLightSample.FloatAt(c)],
                           (float)v[WorldLightSample.FloatAt(d)]);
    }

    /// <summary>_WmvWLOn, written only when it changes (the first write always): outside the world view nothing is written per
    /// camera.</summary>
    static void SetOn(bool value)
    {
        if (HardOff)
            return;
        if (onWritten && publishedOn == value)
            return;
        publishedOn = value;
        onWritten = true;
        Shader.SetGlobalFloat(OnId, value ? 1f : 0f);
    }

    static void OnBeginCamera(ScriptableRenderContext context, Camera camera)
    {
        // Every camera but the main one starts without the light; the main camera's own state is WmvMain's Frame (which may run
        // before or after this handler: its values are the same either way). Nothing to do while it is 0 already.
        if (publishedOn && camera != Camera.main)
            SetOn(false);
    }

    static void OnEndCamera(ScriptableRenderContext context, Camera camera)
    {
        if (publishedOn)
            SetOn(false);
    }

    /// <summary>The self-test's: a camera's begin and end as the pipeline calls them.</summary>
    public static void BeginCameraForTest(Camera camera) { OnBeginCamera(default(ScriptableRenderContext), camera); }
    public static void EndCameraForTest(Camera camera) { OnEndCamera(default(ScriptableRenderContext), camera); }

    /// <summary>setWorldLight (protocol 20): on or off, the clock pinned or running at 1x, the time; a field absent is unchanged, a
    /// value refused is said. A change of the clock or the time snaps the next frame (a clock jump).</summary>
    public static void SetFromHost(WmvIpcClient.WorldLightCommand c)
    {
        if (c == null)
            return;
        if (HardOff)
        {
            if (Log != null)
                Log("world light: " + c + " -- refused: WMV_LIGHT=off (hard off)");
            return;
        }
        var refused = new List<string>();
        if (c.HasOn && c.On != on)
        {
            on = c.On;
            ease.Reset();
        }
        if (c.HasClock)
        {
            if (c.Clock == WorldClock.SourcePinned || c.Clock == WorldClock.SourceRunning)
            {
                bool run = c.Clock == WorldClock.SourceRunning;
                if (run != clock.Running)
                {
                    clock.Running = run;
                    clockJumped = true;
                }
            }
            else
                refused.Add("clock \"" + c.Clock + "\" is not pinned or running");
        }
        if (c.HasTime)
        {
            double s;
            if (WorldClock.TryParse(c.Time, out s))
            {
                clock.Seconds = s;
                clockJumped = true;
            }
            else
                refused.Add("time \"" + c.Time + "\" is not HH:MM[:SS]");
        }
        if (c.HasFarclip)
        {
            if (LightGeometry.Finite(c.Farclip) && c.Farclip > 0.0)
                farClip = c.Farclip;
            else
                refused.Add("farclip " + c.Farclip.ToString("R", CultureInfo.InvariantCulture) + " is not a number of yards above 0");
        }
        Say(string.Format(CultureInfo.InvariantCulture, "world light: {0} -> {1}, {2} ({3}), game far clip {4} yd{5}", c, Enabled ? "on" : "off",
                          WorldClock.Format(clock.Effective), clock.Source, farClip,
                          refused.Count > 0 ? "; refused: " + string.Join("; ", refused.ToArray()) : ""));
        if (!Enabled)
        {
            Off();
            skyDrawn = false;
            WmvWorldSky.HoldBloom(false, Log);
        }
    }

    /// <summary>runtimeState's light fields (protocol 20). A number not available reads -1 (the zone, the Light and the
    /// LightParams of an evaluation that found none).</summary>
    public static void Fill(ref WmvIpcClient.RuntimeReport r)
    {
        r.LightFilled = true;
        r.LightOn = Enabled;
        r.LightTime = WorldClock.Format(clock.Effective);
        r.LightTimeSource = clock.Source;
        r.LightZone = evaluated ? sample.Zone : -1;
        r.LightId = evaluated && sample.Light > 0 ? sample.Light : -1;
        r.LightParams = evaluated && sample.Valid ? sample.Params : -1;
        r.LightValid = evaluated && sample.Valid;
        r.GameFarclip = farClip;
        r.SkyOn = Enabled && skyDrawn;
    }

    /// <summary>The capture log line's field: what the outdoor light state is and what it draws. Not used with WMV_LIGHT=off
    /// (the capture line then stays Stage 09's).</summary>
    public static string Describe()
    {
        if (HardOff)
            return "world light off (WMV_LIGHT=off)";
        if (!Enabled)
            return "world light off";
        if (!evaluated)
            return "world light on: no outdoor light here (" + WorldClock.Format(clock.Effective) + ", " + clock.Source + ")";
        WorldLightSet set = lastSet as WorldLightSet;
        return "world light on: " + StateText() + " -- drawn: " + (skyDrawn ? "the sky" : "no sky") + ", " +
               (Switches.TerrainLight ? "the terrain lit" + (Switches.TerrainFlat ? " (FLAT variant)" : "") : "the terrain unlit (-wmvTerrainLight=off)") +
               Switches.DescribeObjects(set != null ? set.Product : null, fogAbsent);
    }

    static string StateText()
    {
        string from = sample.Zone > 0 ? "zone " + sample.Zone : sample.Zone == 0 ? "map default" : "none";
        string when = string.Format(CultureInfo.InvariantCulture, "{0} (half-minute {1}, {2})", WorldClock.Format(clock.Effective), clock.HalfMinute,
                                    clock.Source);
        WorldLightSet set = lastSet as WorldLightSet;
        string map = set != null ? set.Map.ToString(CultureInfo.InvariantCulture) : "-";
        if (!sample.Valid)
            return string.Format(CultureInfo.InvariantCulture, "none -- {0}{1} (map {2}) at {3}", sample.Why,
                                 sample.Light > 0 ? " (" + from + " light " + sample.Light + ")" : "", map, when);
        return string.Format(CultureInfo.InvariantCulture, "{0} light {1} params {2}{3} (map {4}) at {5}: valid, {6} point light(s); groups {7}; {8}",
                             from, sample.Light, sample.Params, sample.SlotFellBack ? " (slot 0's: the slot has no keys)" : "", map, when, sample.Points,
                             WorldLightSample.DescribeGroups(sample.Groups), FogText(set));
    }

    /// <summary>Stage 10 step 7: the state line's fog clause, this frame's record (packed before the line is said): the record's
    /// own WorldFogRecord.Summary ("fog: start S yd, k K /yd, end E yd, colour (r, g, b)" with the colour in whole bytes -- the
    /// rig's parser reads them so --, the power arm's "fog: power path, ...") or "fog: off (reason)". One formatter: the runner
    /// tests the text the player says.</summary>
    static string FogText(WorldLightSet set)
    {
        WorldFogRecord r = fogRecord;
        if (r.Mode == WorldFogMode.Off)
        {
            string product = set != null && !string.IsNullOrEmpty(set.Product) ? set.Product : null;
            return "fog: off (" + (!Switches.Fog ? "-wmvFog=off" : fogAbsent != null ? "fog columns absent: " + fogAbsent
                                   : fogMode != WorldFogMode.Off ? "no valid light state"
                                   : product != null ? "product " + product + ", D17/D18" : "no product") + ")";
        }
        return r.Summary();
    }

    /// <summary>Stage 10 step 7 (log only): the fog record exactly as published for the render now -- all 14 rows as the GPU gets
    /// them (floats, round-trip; R2.w the plane offset of the eye uploaded, as PublishFog computes it), the eye in Unity, the
    /// camera's and the floating origin's game Z and the plane's -- so the rig can compare the record row by row with its port
    /// (fogmodel.pack). WatchRecord says it once per settled view. "fog record: none (...)" while the light is shown without
    /// fog; null while nothing is shown and with WMV_LIGHT=off.</summary>
    public static string FogRecordText()
    {
        if (HardOff || !Showing)
            return null;
        WorldFogRecord r = fogRecord;
        if (r.Mode == WorldFogMode.Off)
            return "fog record: none (" + FogText(lastSet as WorldLightSet) + ")";
        var inv = CultureInfo.InvariantCulture;
        var sb = new System.Text.StringBuilder(1400);
        sb.Append("fog record as published: mode ").Append(WorldFog.ModeName(r.Mode));
        for (int k = 0; k < WorldFogRecord.Rows; k++)
        {
            int at = 4 * k;
            double w = k == 2 ? WorldFog.PlaneOffset(eye.y + originZ, r) : r.R[at + 3];
            sb.Append("; R").Append(k.ToString(inv)).Append(" (").Append(((float)r.R[at]).ToString("R", inv)).Append(", ")
              .Append(((float)r.R[at + 1]).ToString("R", inv)).Append(", ").Append(((float)r.R[at + 2]).ToString("R", inv)).Append(", ")
              .Append(((float)w).ToString("R", inv)).Append(")");
        }
        sb.Append("; eye (").Append(eye.x.ToString("R", inv)).Append(", ").Append(eye.y.ToString("R", inv)).Append(", ")
          .Append(eye.z.ToString("R", inv)).Append(") Unity; camera game Z ").Append((eye.y + originZ).ToString("R", inv))
          .Append(", origin game Z ").Append(originZ.ToString("R", inv)).Append(", fog height ").Append(r.FogHeight.ToString("R", inv));
        return sb.ToString();
    }

    /// <summary>The state line: only when the object, the evaluation, the validity or the base light changed (one per load and
    /// per change of the base light; no text is made otherwise).</summary>
    static void SayState(WorldLightSet set)
    {
        if (ReferenceEquals(set, saidSet) && evaluated == saidEvaluated && sample.Valid == saidValid && sample.Zone == saidZone &&
            sample.Light == saidLight && sample.Params == saidParams)
            return;
        saidSet = set;
        saidEvaluated = evaluated;
        saidValid = sample.Valid;
        saidZone = sample.Zone;
        saidLight = sample.Light;
        saidParams = sample.Params;
        if (!evaluated)
        {
            if (set != null && Enabled)
                Say("world light: none -- the camera is not placed in the scene");
            return;
        }
        Say("world light: " + StateText());
    }

    static void Say(string line)
    {
        if (HardOff || line == lastLine)
            return;
        lastLine = line;
        if (Log != null)
            Log(line);
    }
}
