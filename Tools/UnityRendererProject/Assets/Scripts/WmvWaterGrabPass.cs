// WmvWaterGrabPass.cs
//
// THE HIGH WATER LOOK'S GRAB (Stage 09 tuning): what the water's HIGH path (Resources/WmvLiquid.shader) reads of the scene
// behind and around it, copied once a frame for the main camera's world view after the opaque world and before the water
// draws:
//   _WmvSceneColor      the colour attachment at full resolution, in the AUTHORED domain (WmvFrameDecodePass decodes the
//                       frame later): the bed the water refracts -- the scene as it is
//   _WmvSceneDepth      the depth attachment's raw device depth as R32F (Resources/WmvWaterGrabDepth.shader, pass 0): the
//                       scene point behind the water (rebuilt through the inverse view-projection) and the reflection march
//   _WmvSceneColorRefl  the reflections' colour: _WmvSceneColor premultiplied by its COVERAGE, the coverage in alpha -- 0
//                       where _WmvSceneDepth is the far plane (the background, the camera's clear colour), else 1
//                       (WmvWaterGrabDepth.shader, pass 1): the sharp reflection
//   _WmvSceneColorBlur  that reflection colour at 1/4 resolution (two bilinear halvings: a 4 x 4 box) and one small blur
//                       (Resources/WmvWaterGrabBlur.shader, its tap spacing TUNABLE as grabBlur), the coverage averaged
//                       with it: the broad reflection. The water reads either as c.rgb + sky (1 - c.a): a texel at a
//                       silhouette mixes the scene with the sky, never with the clear colour (the camera's background, the
//                       host's View > Swap Background Color, the screenshot's black and white mattes)
//   _WmvGrabTexel       (1 / width, 1 / height, width, height) of the copies: the water's screen uv is its pixel times this
//   _WmvWaterGrab       1 once the copies exist, for this camera only -- the shader's switch. Every camera starts at 0 (set
//                       at its beginCameraRendering) and is back at 0 after it (endCameraRendering); WmvTerrainComposite
//                       pins it 0 for its command-buffer renders.
// URP's own textures cannot serve: this player's renderer fills _CameraDepthTexture from a depth-normals prepass that no Wmv
// shader draws in, so it holds none of the world (INFERRED from the pipeline's source), and the pipeline asset's opaque
// texture is half resolution. No URP asset or project setting is changed (the repository does not track them).
//
// WHEN, as WmvFrameDecodePass is installed (the renderer asset is not in the repository, so a script enqueues the pass per
// camera from RenderPipelineManager.beginCameraRendering): only for Camera.main, a game camera whose culling mask shows the
// world layer (30), while a liquid object is alive (WmvLiquidMeshes.LiveObjects), and never with WMV_WATER=low (read once:
// the old look, for A/B and as a safety switch). Event BeforeRenderingTransparents -- after the opaque world and the sky,
// before the water's queue (Transparent-1) -- with requiresIntermediateTexture, so the attachments are readable textures.
// The other viewers stay as they were by construction: their cameras never show layer 30, so the pass is never enqueued
// for them, and it never writes the colour attachment.

using System;
using UnityEngine;
using UnityEngine.Experimental.Rendering;
using UnityEngine.Rendering;
using UnityEngine.Rendering.RenderGraphModule;
using UnityEngine.Rendering.RenderGraphModule.Util;
using UnityEngine.Rendering.Universal;

public sealed class WmvWaterGrabPass : ScriptableRenderPass
{
    public const string DepthShaderResource = "WmvWaterGrabDepth", BlurShaderResource = "WmvWaterGrabBlur";
    public static readonly int SceneColorId = Shader.PropertyToID("_WmvSceneColor"), SceneColorBlurId = Shader.PropertyToID("_WmvSceneColorBlur"),
                               SceneDepthId = Shader.PropertyToID("_WmvSceneDepth"), GrabTexelId = Shader.PropertyToID("_WmvGrabTexel"),
                               WaterGrabId = Shader.PropertyToID("_WmvWaterGrab"), BlurStepId = Shader.PropertyToID("_WmvGrabBlurStep"),
                               SceneColorReflId = Shader.PropertyToID("_WmvSceneColorRefl");
    /// <summary>WmvWaterGrabDepth.shader's passes: the raw depth copy, and the colour premultiplied by its coverage.</summary>
    public const int DepthPass = 0, CoveragePass = 1;

    static WmvWaterGrabPass installed;
    static object owner;                    // who installed it through Configure: the only one Release removes it for
    static Material depthMaterial, blurMaterial;
    static int low = -1;                    // WMV_WATER=low, read once: -1 not read yet

    /// <summary>The self-test's: enqueue for Camera.main's world view even with no liquid object alive.</summary>
    public static bool ForceForTest;
    /// <summary>The self-test's: the passes recorded (the frame and the copies' size of the last).</summary>
    public static int Runs, LastFrame = -1, LastWidth, LastHeight;
    static RTHandle captureHandle;          // the self-test's copy of _WmvSceneDepth (CaptureDepthForTest)
    static RTHandle captureReflHandle;      // and of _WmvSceneColorRefl (CaptureReflectionForTest)

    /// <summary>Is the pass installed right now? The High look is on exactly then (WmvLiquidMaterials: a transparent water or
    /// ocean type made meanwhile holds its ripple map and writes depth).</summary>
    public static bool Active { get { return installed != null; } }

    /// <summary>Who installed the pass through Configure (null: nobody, or installed through SetActive alone).</summary>
    public static object Owner { get { return owner; } }

    /// <summary>The pass's own materials alive (0 or 2: made at its install, destroyed at its removal) -- the self-test's
    /// material tallies leave them out.</summary>
    public static int MaterialCount { get { return (depthMaterial != null ? 1 : 0) + (blurMaterial != null ? 1 : 0); } }

    /// <summary>WMV_WATER=low (the environment, read once): the High look off, the zone and fallback paths only.</summary>
    public static bool Low
    {
        get
        {
            if (low < 0)
                low = string.Equals(Environment.GetEnvironmentVariable("WMV_WATER"), "low", StringComparison.OrdinalIgnoreCase) ? 1 : 0;
            return low == 1;
        }
    }

    /// <summary>The terrain session's setup (WmvMain): installs the pass unless WMV_WATER=low -- `who` its owner when this
    /// call installs it (Release(who) removes it; another caller's Release does not) -- puts the neutral sky in place, and
    /// says once what the water does and its tune ("water tune: ..." -- the -wmvWaterTune overrides, WmvLiquidMaterials.Tune).</summary>
    public static void Configure(object who, Action<string> log)
    {
        if (log != null)
            log("water tune: " + WmvLiquidMaterials.Tune.Describe());
        Shader.SetGlobalFloat(WaterGrabId, 0f);
        Shader.SetGlobalVector(WmvLiquidMaterials.ReflectSkyId, WmvLiquidMaterials.NeutralSky);
        if (Low)
        {
            SetActive(false);
            if (log != null)
                log("water: WMV_WATER=low -- the High look is off (the zone and fallback paths only)");
            return;
        }
        bool was = Active;
        bool on = SetActive(true);
        if (on && !was)
            owner = who;
        if (log != null)
            log(on ? "water: the High look (the grab pass for the main camera's world view: Resources/" + DepthShaderResource + ", " +
                     BlurShaderResource + ")"
                   : "water: Resources/" + DepthShaderResource + ".shader or " + BlurShaderResource + ".shader is missing from this player " +
                     "build -- the High look is off");
    }

    /// <summary>The terrain session's end (WmvMain.DisposeTerrain): removes the pass when `who` installed it (Configure), and
    /// only then -- a second WmvMain (the lifecycle self-test's own) disposing its terrain leaves the player's pass alone.
    /// True when it removed it.</summary>
    public static bool Release(object who)
    {
        if (who == null || who != owner)
            return false;
        owner = null;
        SetActive(false);
        return true;
    }

    /// <summary>Install or remove the pass. Idempotent. False when a shader is missing (nothing is installed: the water keeps
    /// the zone and fallback paths). Removing it destroys its two materials (made again at the next install). The owner
    /// (Configure) is kept: the self-test removes and puts back the pass around its checks, and the owner still removes
    /// it at its end.</summary>
    public static bool SetActive(bool on)
    {
        if (on)
        {
            if (installed != null)
                return true;
            if (depthMaterial == null)
                depthMaterial = MaterialOf(DepthShaderResource, "Wmv/WaterGrabDepth");
            if (blurMaterial == null)
                blurMaterial = MaterialOf(BlurShaderResource, "Wmv/WaterGrabBlur");
            if (depthMaterial == null || blurMaterial == null)
                return false;
            blurMaterial.SetFloat(BlurStepId, (float)WmvLiquidMaterials.Tune[Wmv.Wow.World.LiquidHighTune.GrabBlur]);
            installed = new WmvWaterGrabPass();
            RenderPipelineManager.beginCameraRendering += OnBeginCamera;
            RenderPipelineManager.endCameraRendering += OnEndCamera;
            return true;
        }
        if (installed != null)
        {
            RenderPipelineManager.beginCameraRendering -= OnBeginCamera;
            RenderPipelineManager.endCameraRendering -= OnEndCamera;
            installed = null;
        }
        if (depthMaterial != null)
            UnityEngine.Object.DestroyImmediate(depthMaterial);
        if (blurMaterial != null)
            UnityEngine.Object.DestroyImmediate(blurMaterial);
        depthMaterial = null;
        blurMaterial = null;
        Shader.SetGlobalFloat(WaterGrabId, 0f);
        return true;
    }

    static Material MaterialOf(string resource, string name)
    {
        // The way WmvFrameDecodePass finds its shader: the asset by name first, the shader registry second.
        Shader s = Resources.Load<Shader>(resource);
        if (s == null)
            s = Shader.Find(name);
        if (s == null || !s.isSupported)
            return null;
        return new Material(s) { hideFlags = HideFlags.HideAndDontSave };
    }

    /// <summary>Does `camera` get the grab: Camera.main, a game camera showing the world layer, with a liquid object alive
    /// (or ForceForTest)?</summary>
    public static bool Wants(Camera camera)
    {
        if (camera == null || camera.cameraType != CameraType.Game || camera != Camera.main)
            return false;
        if ((camera.cullingMask & WmvTerrainBuilder.TerrainViewMask) == 0)
            return false;
        if (camera.targetTexture != null && camera.targetTexture.format == RenderTextureFormat.Depth)
            return false;
        return WmvLiquidMeshes.LiveObjects > 0 || ForceForTest;
    }

    /// <summary>The self-test's: copy _WmvSceneDepth into `target` (R32F, the camera's size) at every grab from now on; null
    /// stops it.</summary>
    public static void CaptureDepthForTest(RenderTexture target)
    {
        if (captureHandle != null)
        {
            RTHandles.Release(captureHandle);
            captureHandle = null;
        }
        if (target != null)
            captureHandle = RTHandles.Alloc(target);
    }

    /// <summary>The self-test's: copy _WmvSceneColorRefl into `target` (a float RGBA target of the camera's size) at every
    /// grab from now on; null stops it.</summary>
    public static void CaptureReflectionForTest(RenderTexture target)
    {
        if (captureReflHandle != null)
        {
            RTHandles.Release(captureReflHandle);
            captureReflHandle = null;
        }
        if (target != null)
            captureReflHandle = RTHandles.Alloc(target);
    }

    WmvWaterGrabPass()
    {
        renderPassEvent = RenderPassEvent.BeforeRenderingTransparents;
        requiresIntermediateTexture = true;
        profilingSampler = new ProfilingSampler("WmvWaterGrab");
    }

    static void OnBeginCamera(ScriptableRenderContext context, Camera camera)
    {
        // Every camera starts without the grab; the pass sets 1 for its own once the copies exist.
        Shader.SetGlobalFloat(WaterGrabId, 0f);
        if (installed == null || !Wants(camera))
            return;
        UniversalAdditionalCameraData data = camera.GetUniversalAdditionalCameraData();
        if (data == null || data.scriptableRenderer == null)
            return;
        data.scriptableRenderer.EnqueuePass(installed);
    }

    static void OnEndCamera(ScriptableRenderContext context, Camera camera)
    {
        Shader.SetGlobalFloat(WaterGrabId, 0f);
    }

    sealed class DepthPassData
    {
        public TextureHandle Source;
        public Material Material;
        public Vector4 Texel;
    }

    public override void RecordRenderGraph(RenderGraph renderGraph, ContextContainer frameData)
    {
        if (depthMaterial == null || blurMaterial == null)
            return;
        UniversalResourceData resources = frameData.Get<UniversalResourceData>();
        if (resources.isActiveTargetBackBuffer)
            return;
        TextureHandle colour = resources.activeColorTexture, depth = resources.activeDepthTexture;
        if (!colour.IsValid() || !depth.IsValid())
            return;
        RenderTargetInfo colourInfo = renderGraph.GetRenderTargetInfo(colour), depthInfo = renderGraph.GetRenderTargetInfo(depth);
        // A multisampled attachment would need a resolve first (never so here: the pipeline asset's MSAA is off): no grab,
        // the water keeps its old paths.
        if (colourInfo.msaaSamples > 1 || depthInfo.msaaSamples > 1 || colourInfo.width <= 0 || colourInfo.height <= 0)
            return;
        int w = colourInfo.width, h = colourInfo.height;
        // The reflection colour carries its coverage in alpha: the attachment's format when it has an alpha channel, else a
        // half-float RGBA one.
        GraphicsFormat reflFormat = GraphicsFormatUtility.GetComponentCount(colourInfo.format) >= 4 ? colourInfo.format
                                                                                                  : GraphicsFormat.R16G16B16A16_SFloat;

        TextureHandle sceneColor = renderGraph.CreateTexture(Desc(w, h, colourInfo.format, FilterMode.Bilinear, "_WmvSceneColor"));
        TextureHandle reflColor = renderGraph.CreateTexture(Desc(w, h, reflFormat, FilterMode.Bilinear, "_WmvSceneColorRefl"));
        TextureHandle half = renderGraph.CreateTexture(Desc(Math.Max(1, w / 2), Math.Max(1, h / 2), reflFormat, FilterMode.Bilinear,
                                                            "_WmvSceneColorHalf"));
        TextureHandle quarter = renderGraph.CreateTexture(Desc(Math.Max(1, w / 4), Math.Max(1, h / 4), reflFormat, FilterMode.Bilinear,
                                                               "_WmvSceneColorQuarter"));
        TextureHandle blur = renderGraph.CreateTexture(Desc(Math.Max(1, w / 4), Math.Max(1, h / 4), reflFormat, FilterMode.Bilinear,
                                                            "_WmvSceneColorBlur"));
        TextureHandle sceneDepth = renderGraph.CreateTexture(Desc(w, h, GraphicsFormat.R32_SFloat, FilterMode.Point, "_WmvSceneDepth"));

        // (1) The colour as it stands (the opaque world and the sky), full resolution: the bed.
        using (IBaseRenderGraphBuilder b = renderGraph.AddBlitPass(colour, sceneColor, Vector2.one, Vector2.zero, passName: "WmvWaterGrab: colour",
                                                                    returnBuilder: true))
        {
            b.AllowPassCulling(false);
            b.SetGlobalTextureAfterPass(sceneColor, SceneColorId);
        }
        // (2) The raw depth, then the switch: _WmvWaterGrab 1 and the copies' texel size, set in the command stream (every
        // pass here runs before the water's transparent pass, which reads every global).
        using (IRasterRenderGraphBuilder b = renderGraph.AddRasterRenderPass("WmvWaterGrab: depth", out DepthPassData d))
        {
            d.Source = depth;
            d.Material = depthMaterial;
            d.Texel = new Vector4(1f / w, 1f / h, w, h);
            b.UseTexture(depth, AccessFlags.Read);
            b.SetRenderAttachment(sceneDepth, 0, AccessFlags.Write);
            b.AllowPassCulling(false);
            b.AllowGlobalStateModification(true);
            b.SetGlobalTextureAfterPass(sceneDepth, SceneDepthId);
            b.SetRenderFunc(static (DepthPassData data, RasterGraphContext context) =>
            {
                Blitter.BlitTexture(context.cmd, data.Source, new Vector4(1f, 1f, 0f, 0f), data.Material, DepthPass);
                context.cmd.SetGlobalVector(GrabTexelId, data.Texel);
                context.cmd.SetGlobalFloat(WaterGrabId, 1f);
            });
        }
        // (3) The reflections' colour: the scene premultiplied by its coverage (the background 0), read with the depth copy.
        using (IBaseRenderGraphBuilder b = renderGraph.AddBlitPass(new RenderGraphUtils.BlitMaterialParameters(sceneColor, reflColor, depthMaterial,
                                                                                                                CoveragePass),
                                                                    passName: "WmvWaterGrab: coverage", returnBuilder: true))
        {
            b.UseGlobalTexture(SceneDepthId, AccessFlags.Read);
            b.AllowPassCulling(false);
            b.SetGlobalTextureAfterPass(reflColor, SceneColorReflId);
        }
        // (4) Two bilinear halvings of it (each texel the mean of 2 x 2, the coverage too), then the small blur at 1/4.
        using (IBaseRenderGraphBuilder b = renderGraph.AddBlitPass(reflColor, half, Vector2.one, Vector2.zero, passName: "WmvWaterGrab: 1/2",
                                                                    returnBuilder: true))
            b.AllowPassCulling(false);
        using (IBaseRenderGraphBuilder b = renderGraph.AddBlitPass(half, quarter, Vector2.one, Vector2.zero, passName: "WmvWaterGrab: 1/4",
                                                                    returnBuilder: true))
            b.AllowPassCulling(false);
        using (IBaseRenderGraphBuilder b = renderGraph.AddBlitPass(new RenderGraphUtils.BlitMaterialParameters(quarter, blur, blurMaterial, 0),
                                                                    passName: "WmvWaterGrab: blur", returnBuilder: true))
        {
            b.AllowPassCulling(false);
            b.SetGlobalTextureAfterPass(blur, SceneColorBlurId);
        }
        if (captureHandle != null && captureHandle.rt != null && captureHandle.rt.width == w && captureHandle.rt.height == h)
        {
            TextureHandle capture = renderGraph.ImportTexture(captureHandle);
            using (IBaseRenderGraphBuilder b = renderGraph.AddBlitPass(sceneDepth, capture, Vector2.one, Vector2.zero,
                                                                        passName: "WmvWaterGrab: the self-test's depth copy", returnBuilder: true))
                b.AllowPassCulling(false);
        }
        if (captureReflHandle != null && captureReflHandle.rt != null && captureReflHandle.rt.width == w && captureReflHandle.rt.height == h)
        {
            TextureHandle capture = renderGraph.ImportTexture(captureReflHandle);
            using (IBaseRenderGraphBuilder b = renderGraph.AddBlitPass(reflColor, capture, Vector2.one, Vector2.zero,
                                                                        passName: "WmvWaterGrab: the self-test's reflection copy", returnBuilder: true))
                b.AllowPassCulling(false);
        }
        Runs++;
        LastFrame = Time.frameCount;
        LastWidth = w;
        LastHeight = h;
    }

    static TextureDesc Desc(int w, int h, GraphicsFormat format, FilterMode filter, string name)
    {
        return new TextureDesc(w, h)
        {
            format = format, filterMode = filter, wrapMode = TextureWrapMode.Clamp, name = name, clearBuffer = false,
            msaaSamples = MSAASamples.None, useMipMap = false,
        };
    }
}
