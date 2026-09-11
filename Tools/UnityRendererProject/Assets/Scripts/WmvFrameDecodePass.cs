// WmvFrameDecodePass.cs
//
// Decodes the finished frame from the authored domain into linear light, once, before
// post-processing -- so that every hardware blend before it has run on AUTHORED values, the way
// Wowhead's viewer, the legacy OpenGL viewport and the game blend, and so that Bloom and the
// swapchain still see exactly what they saw before for any pixel that was not blended.
//
// WHY A PASS ENQUEUED FROM SCRIPT
//   The repository versions only Assets/Scripts and Assets/Resources of the Unity project; the
//   renderer asset is not in it, so a renderer feature registered on that asset would not be a
//   change anyone could rebuild from source. URP lets a script enqueue a pass per camera instead
//   (RenderPipelineManager.beginCameraRendering -> ScriptableRenderer.EnqueuePass), and in
//   RenderGraph mode such a pass runs through RecordRenderGraph like any other. That keeps the
//   whole change in the repository.
//
// WHAT IT DOES, PRECISELY
//   At BeforeRenderingPostProcessing -- after the opaque, transparent and emitter passes, before
//   UberPost (Bloom) -- copy the camera colour to a temporary of the same description and draw it
//   back through Wmv/FrameDecode, which applies the exact sRGB EOTF to rgb and leaves alpha and
//   values above 1 alone. It is the URP FullScreenPassRendererFeature's own RenderGraph shape,
//   with the material fixed. requiresIntermediateTexture is set so the camera never renders
//   straight into a back buffer this pass could not read.
//
//   It runs on EVERY camera that renders while the transform is on, including the offscreen
//   probe cameras (-wmvLightCheck, -wmvQueueProof): their shaders now write authored values too,
//   and their readbacks are only right if the same decode precedes the final blit into their
//   sRGB targets.
//
// The switch is ConfigureDisplayTransform's: 'full' installs this pass and turns the shaders'
// per-fragment encode off; 'fragment' keeps the old per-fragment encode with no pass, so the two
// can be captured from one build and differenced.

using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.RenderGraphModule;
using UnityEngine.Rendering.RenderGraphModule.Util;
using UnityEngine.Rendering.Universal;

public sealed class WmvFrameDecodePass : ScriptableRenderPass
{
    static WmvFrameDecodePass installed;
    static Material material;

    /// <summary>Is the frame decode installed on every camera right now?</summary>
    public static bool Active { get { return installed != null; } }

    /// <summary>
    /// Install or remove the pass. Idempotent. Returns false when the shader is missing, in which
    /// case nothing is installed and the caller must not turn the fragment encode off.
    /// </summary>
    public static bool SetActive(bool on)
    {
        if (on)
        {
            if (installed != null)
                return true;
            if (material == null)
            {
                // The way the other two Resources shaders are found (WmvModelBuilder): the asset
                // by name first, the shader registry second.
                Shader s = Resources.Load<Shader>("WmvFrameDecode");
                if (s == null)
                    s = Shader.Find("Wmv/FrameDecode");
                if (s == null)
                    return false;
                material = new Material(s);
                material.hideFlags = HideFlags.HideAndDontSave;
            }
            installed = new WmvFrameDecodePass();
            RenderPipelineManager.beginCameraRendering += OnBeginCamera;
            return true;
        }
        if (installed != null)
        {
            RenderPipelineManager.beginCameraRendering -= OnBeginCamera;
            installed = null;
        }
        return true;
    }

    WmvFrameDecodePass()
    {
        renderPassEvent = RenderPassEvent.BeforeRenderingPostProcessing;
        requiresIntermediateTexture = true;
        profilingSampler = new ProfilingSampler("WmvFrameDecode");
    }

    static void OnBeginCamera(ScriptableRenderContext context, Camera camera)
    {
        if (installed == null || camera == null)
            return;
        // Scene-view, preview and reflection cameras never show this content; the game camera
        // and the offscreen probes do. A camera rendering into a DEPTH texture -- WmvShadowRig's
        // shadow map and view-depth cameras -- has no colour to decode, and URP still records the
        // post-processing event on it, so it is skipped by the pipeline's own test for that case
        // (UniversalRenderer.IsOffscreenDepthTexture).
        if (camera.cameraType != CameraType.Game)
            return;
        if (camera.targetTexture != null && camera.targetTexture.format == RenderTextureFormat.Depth)
            return;
        UniversalAdditionalCameraData data = camera.GetUniversalAdditionalCameraData();
        if (data == null || data.scriptableRenderer == null)
            return;
        data.scriptableRenderer.EnqueuePass(installed);
    }

    public override void RecordRenderGraph(RenderGraph renderGraph, ContextContainer frameData)
    {
        if (material == null)
            return;
        UniversalResourceData resources = frameData.Get<UniversalResourceData>();
        TextureHandle target = resources.activeColorTexture;
        if (!target.IsValid())
            return;

        // The decode reads the whole frame and writes the whole frame, so it goes through a copy:
        // a pass cannot sample the attachment it is rendering into.
        TextureDesc desc = renderGraph.GetTextureDesc(target);
        desc.name = "_WmvFrameDecodeSource";
        desc.clearBuffer = false;
        TextureHandle copy = renderGraph.CreateTexture(desc);

        renderGraph.AddBlitPass(target, copy, Vector2.one, Vector2.zero,
                                passName: "WmvFrameDecode: copy");
        renderGraph.AddBlitPass(new RenderGraphUtils.BlitMaterialParameters(copy, target, material, 0),
                                passName: "WmvFrameDecode: authored -> linear");
    }
}
