// WmvScreenshot.cs
//
// THE VIEWPORT SCREENSHOT (protocol 6). captureScreenshot { request, path, width, height } renders what the
// viewport shows again, off screen, at width x height with a transparent background, and writes it to path as an
// RGBA PNG; screenshotSaved answers with the outcome and the timings. The host sends
// 3840 x 2160 and a path its Save As dialog chose (and confirmed, when the file exists).
//
// WHAT IS RENDERED. The live viewport camera itself: its pose, culling, pipeline, the frame decode
// (WmvFrameDecodePass) and every renderer on screen -- the model, what a character wears, the mount it rides,
// particles and ribbons as they are this frame. Nothing is rebuilt, reloaded, re-simulated or hidden. It is
// rendered TWICE, once over black and once over white, and the two renders make the PNG's colour and alpha:
//   - each into a width x height FP16 target: URP renders straight into an external target's format
//     (UniversalRenderPipelineCore.CreateRenderTextureDescriptor), and FP16 is the precision the viewport's own
//     buffer is given for the authored-domain blends (ConfigureDisplayTransform);
//   - cleared to (0,0,0,0) the first time and to (1,1,1,0) the second instead of the viewport's (25,25,30). Black
//     and white come through ClearColour unchanged. The clear IS the preview's whole background: the scene has no
//     ground, backdrop or shadow-receiver geometry (the contact shadows are computed on the model's own surfaces,
//     see WmvShadowRig), so there is nothing else to leave out;
//   - then the matte (Resources/WmvScreenshotMatte.shader) reads the same pixel of both, in the authored domain the
//     blends ran in, and writes straight colour and coverage into an 8-bit target that is read back as the PNG's
//     bytes. The rendered alpha is not used: the materials blend it with their colour factors, which made an
//     additive glow's quad opaque and an alpha-blended edge a * a. Coverage is what a pixel lets the background
//     through, 1 - max(W - B), raised where needed so the straight colour B / a fits in [0, 1]: the PNG over black
//     is the black render, opaque stays 255, the empty background 0, and an additive glow's alpha follows its
//     light. Light added over a background has no exact straight-alpha form: such a glow is exact over black,
//     close over dark backgrounds and only approximate over light ones (a coloured glow shows its colour over
//     white, where the render over white stays white);
//   - the capture's aspect with the vertical field of view kept, so a viewport as wide as the capture or narrower
//     shows everything it shows now, with more at the sides; a viewport wider than the capture keeps its
//     horizontal field of view instead, so nothing it shows is cropped (CaptureFieldOfView);
//   - no post-processing. What that pass does for the viewport is bloom alone -- tone mapping and vignette are set
//     to nothing (ConfigureDisplayTransform) -- and the PNG leaves bloom out. (URP's post pass also writes alpha 1
//     unless the pipeline asset allows alpha output there, which the player's does not: UniversalRenderPipeline.
//     InitializeAdditionalCameraData, UberPost.shader);
//   - the shadow rig's maps rendered for that projection (WmvShadowRig.RenderFor), and for the live camera again
//     afterwards.
// Everything is put back in a finally and the targets, the material and the texture are released, whatever
// failed, and the log line compares the camera after the capture with the camera before it.
//
// WHEN. At the end of the frame the request arrives in: after every Update and LateUpdate -- the animators' clocks,
// the poses, the emitters' step and their billboards -- and after the viewport has drawn that frame. Both renders,
// the matte, the readback, the encode and the write run synchronously there, so no clock can advance between the
// two renders or after them, and the PNG is the frame the viewport presents. The frame after it takes the real
// time the capture took, as after any long frame (WmvM2Animator.LateUpdate); nothing is restarted.

using System;
using System.IO;
using UnityEngine;
using UnityEngine.Experimental.Rendering;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

public partial class WmvMain
{
    /// <summary>The largest side a capture may ask for (the host asks for 3840 x 2160).</summary>
    const int ScreenshotMaxSide = 8192;

    bool screenshotBusy;              // a capture is waiting for the end of its frame
    int screenshotsTaken;             // captures attempted, numbered in the log

    /// <summary>captureScreenshot: refuse what cannot be captured at once, otherwise capture at the end of this frame.</summary>
    void HandleCaptureScreenshot(WmvIpcClient.ScreenshotRequest req)
    {
        string why = ScreenshotRequestProblem(req);
        if (why != null)
        {
            Debug.LogWarning("WMV: screenshot " + req.request + " refused: " + why);
            ipc.ReportScreenshotSaved(new WmvIpcClient.ScreenshotReport
            {
                Request = req.request, Ok = false, Error = why, Path = req.path, Width = req.width, Height = req.height,
            });
            return;
        }
        screenshotBusy = true;
        StartCoroutine(CaptureScreenshotAtEndOfFrame(req));
    }

    /// <summary>Why this request cannot be captured, or null.</summary>
    string ScreenshotRequestProblem(WmvIpcClient.ScreenshotRequest req)
    {
        if (screenshotBusy)
            return "a screenshot is already being taken";
        if (req.width < 16 || req.height < 16 || req.width > ScreenshotMaxSide || req.height > ScreenshotMaxSide)
            return string.Format("unsupported size {0} x {1}", req.width, req.height);
        if (Camera.main == null)
            return "the viewport has no camera";
        if (currentSlot.Runtime == null && currentMapObject == null)
            return "nothing is shown in the viewport";
        if (string.IsNullOrEmpty(req.path))
            return "no file name was given";
        try
        {
            if (!Path.IsPathRooted(req.path))
                return "the file name is not a full path: " + req.path;
            if (!string.Equals(Path.GetExtension(req.path), ".png", StringComparison.OrdinalIgnoreCase))
                return "the file name does not end in .png: " + req.path;
            string folder = Path.GetDirectoryName(Path.GetFullPath(req.path));
            if (string.IsNullOrEmpty(folder) || !Directory.Exists(folder))
                return "the folder does not exist: " + folder;
            if (Directory.Exists(req.path))
                return "a folder has that name: " + req.path;
        }
        catch (Exception e)
        {
            return "the file name cannot be used (" + e.Message + "): " + req.path;
        }
        return null;
    }

    System.Collections.IEnumerator CaptureScreenshotAtEndOfFrame(WmvIpcClient.ScreenshotRequest req)
    {
        int n = screenshotsTaken + 1;
        // VALIDATION ONLY (WMV_VIEWPORT_SHOT, see RequestViewportShot): the frame the viewport presents, captured just
        // before the off-screen render and again a frame after it, so a run can show the viewport did not change. With
        // WMV_VIEWPORT_SIZE set as well, the screen is first asked for that size again, as CaptureViewportWhenSettled
        // asks (the host resizes the player to its pane whenever it lays out its panels), so runs can capture the same
        // scene from viewports of different sizes.
        string shot = System.Environment.GetEnvironmentVariable("WMV_VIEWPORT_SHOT");
        bool bracket = !string.IsNullOrEmpty(shot);
        int want = bracket && !string.IsNullOrEmpty(System.Environment.GetEnvironmentVariable("WMV_VIEWPORT_SIZE"))
                   ? RequestedViewportSize() : 0;
        if (want > 0 && (Screen.width != want || Screen.height != want))
        {
            int fromW = Screen.width, fromH = Screen.height;
            Screen.SetResolution(want, want, false);
            for (int i = 0; i < 30 && (Screen.width != want || Screen.height != want); i++)
                yield return null;
            Debug.Log(string.Format("WMV: screenshot {0}: the screen was {1}x{2}, asked for {3}x{3} again -> {4}x{5}",
                                    n, fromW, fromH, want, Screen.width, Screen.height));
            Camera cam = Camera.main;
            if (haveLastFramed && cam != null && Mathf.Abs(cam.aspect - lastFramedAspect) > 1e-3f)
            {
                orbit.Frame(lastFramed);
                lastFramedAspect = cam.aspect;
                ApplyViewportOrbitOverride("screenshot: ");
            }
            for (int i = 0; i < 5; i++)
                yield return new WaitForEndOfFrame();
        }
        yield return new WaitForEndOfFrame();
        if (bracket)
            TakeViewportShot(shot + "-before-screenshot-" + n, null);
        WmvIpcClient.ScreenshotReport report;
        try
        {
            report = TakeScreenshot(req, n);
        }
        finally
        {
            screenshotsTaken = n;
            screenshotBusy = false;
        }
        ipc.ReportScreenshotSaved(report);
        if (!bracket)
            yield break;
        yield return new WaitForEndOfFrame();
        TakeViewportShot(shot + "-after-screenshot-" + n, null);
        // What the pipeline still holds once its pooled 4K targets have gone unused for a while.
        for (int i = 0; i < 60; i++)
            yield return null;
        Debug.Log(string.Format("WMV: screenshot {0}: 60 frames later graphics driver memory {1:F1} MB, {2} render texture(s)",
                                n, UnityEngine.Profiling.Profiler.GetAllocatedMemoryForGraphicsDriver() / 1048576.0,
                                Resources.FindObjectsOfTypeAll<RenderTexture>().Length));
    }

    /// <summary>The live camera's state a capture changes or could disturb, to put back and to compare.</summary>
    struct ScreenshotCameraState
    {
        public RenderTexture TargetTexture;
        public CameraClearFlags ClearFlags;
        public Color Background;
        public float FieldOfView, Aspect, Near, Far;
        public Vector3 Position;
        public Quaternion Rotation;
        public int PixelWidth, PixelHeight, ScreenWidth, ScreenHeight, CullingMask;
        public bool PostProcessing;

        public static ScreenshotCameraState Of(Camera cam, UniversalAdditionalCameraData data)
        {
            return new ScreenshotCameraState
            {
                TargetTexture = cam.targetTexture, ClearFlags = cam.clearFlags, Background = cam.backgroundColor,
                FieldOfView = cam.fieldOfView, Aspect = cam.aspect, Near = cam.nearClipPlane, Far = cam.farClipPlane,
                Position = cam.transform.position, Rotation = cam.transform.rotation,
                PixelWidth = cam.pixelWidth, PixelHeight = cam.pixelHeight, ScreenWidth = Screen.width,
                ScreenHeight = Screen.height, CullingMask = cam.cullingMask,
                PostProcessing = data != null && data.renderPostProcessing,
            };
        }

        /// <summary>Every field that differs from was, or "" when none does. Exact comparisons: nothing is recomputed.</summary>
        public string DifferencesFrom(ScreenshotCameraState was)
        {
            var d = new System.Collections.Generic.List<string>();
            if (TargetTexture != was.TargetTexture) d.Add("target texture");
            if (ClearFlags != was.ClearFlags) d.Add("clear flags " + was.ClearFlags + " -> " + ClearFlags);
            if (Background != was.Background) d.Add("background " + was.Background + " -> " + Background);
            if (FieldOfView != was.FieldOfView) d.Add("field of view " + was.FieldOfView + " -> " + FieldOfView);
            if (Aspect != was.Aspect) d.Add("aspect " + was.Aspect + " -> " + Aspect);
            if (Near != was.Near || Far != was.Far) d.Add("clip planes");
            if (Position != was.Position || Rotation != was.Rotation) d.Add("pose");
            if (PixelWidth != was.PixelWidth || PixelHeight != was.PixelHeight) d.Add("pixel size");
            if (ScreenWidth != was.ScreenWidth || ScreenHeight != was.ScreenHeight) d.Add("screen size");
            if (CullingMask != was.CullingMask) d.Add("culling mask");
            if (PostProcessing != was.PostProcessing) d.Add("post-processing");
            return string.Join(", ", d.ToArray());
        }

        public string Describe()
        {
            var inv = System.Globalization.CultureInfo.InvariantCulture;
            return string.Format(inv, "camera at ({0:F3}, {1:F3}, {2:F3}) rotation ({3:F4}, {4:F4}, {5:F4}, {6:F4}), field of view {7:0.###}, " +
                                 "aspect {8:F4}, clip {9:0.####}..{10:0.##}, {11} x {12} px on a {13} x {14} screen, target {15}, " +
                                 "clear {16} ({17:F4}, {18:F4}, {19:F4}, {20:F4}), post-processing {21}",
                                 Position.x, Position.y, Position.z, Rotation.x, Rotation.y, Rotation.z, Rotation.w, FieldOfView,
                                 Aspect, Near, Far, PixelWidth, PixelHeight, ScreenWidth, ScreenHeight,
                                 TargetTexture == null ? "the screen" : TargetTexture.name, ClearFlags, Background.r, Background.g,
                                 Background.b, Background.a, PostProcessing ? "on" : "off");
        }
    }

    /// <summary>
    /// The vertical field of view that shows at least what the viewport shows, at the capture's aspect. A viewport as
    /// wide as the capture or narrower keeps its own: the capture shows the same height and more at the sides. A wider
    /// viewport keeps its horizontal angle instead, the smallest change that crops nothing it shows.
    /// </summary>
    static float CaptureFieldOfView(float liveFov, float liveAspect, float captureAspect)
    {
        if (liveAspect <= captureAspect || captureAspect <= 0f)
            return liveFov;
        double tanHalfH = Math.Tan(liveFov * 0.5 * Math.PI / 180.0) * liveAspect;
        return (float)(2.0 * Math.Atan(tanHalfH / captureAspect) * 180.0 / Math.PI);
    }

    /// <summary>The clocks and live particles of what is on screen, for the capture's log: the model (or the character),
    /// and the mount it rides.</summary>
    string DescribeScreenshotScene()
    {
        WmvRuntimeModel model = currentSlot.Runtime;
        WmvRuntimeModel ridden = model != null && mounted != null && mounted.RiddenFileDataID != 0 ? mounted.Mount.Runtime : null;
        string s = model == null ? "a world model" : DescribeClock("the model " + currentSlot.FileDataID, currentSlot) +
                   string.Format(", {0} live particle(s), {1} ribbon segment(s)",
                                 model.Emitters != null ? model.Emitters.LiveParticleCount : 0,
                                 model.Emitters != null ? model.Emitters.RibbonSegmentCount : 0);
        if (ridden != null)
            s += "; " + DescribeClock("the mount " + mounted.RiddenFileDataID, mounted.Mount) +
                 string.Format(", {0} live particle(s), {1} ribbon segment(s)",
                               ridden.Emitters != null ? ridden.Emitters.LiveParticleCount : 0,
                               ridden.Emitters != null ? ridden.Emitters.RibbonSegmentCount : 0);
        return s;
    }

    static readonly int OverBlackId = Shader.PropertyToID("_WmvOverBlack");
    static readonly int OverWhiteId = Shader.PropertyToID("_WmvOverWhite");

    /// <summary>A render texture of the capture's, created now; destroyed again if it cannot be.</summary>
    static RenderTexture NewScreenshotTexture(RenderTextureDescriptor desc, string name)
    {
        var rt = new RenderTexture(desc) { name = name };
        if (rt.Create())
            return rt;
        DestroyImmediate(rt);
        throw new InvalidOperationException(string.Format("could not create the {0} x {1} texture {2}", desc.width, desc.height, name));
    }

    /// <summary>A CPU-only texture ReadPixels fills and the encoder reads: never uploaded -- created normally, its
    /// blank 33 MB would be (the D3D12 log warned about the upload's size).</summary>
    static Texture2D NewScreenshotPixels(int width, int height, GraphicsFormat format, string name)
    {
        return new Texture2D(width, height, format, TextureCreationFlags.DontInitializePixels | TextureCreationFlags.DontUploadUponCreate)
        {
            name = name,
        };
    }

    /// <summary>One of the capture's renders: the camera, already set up for the capture, into target over clear.</summary>
    static void RenderScreenshotPass(Camera cam, RenderTexture target, Color clear)
    {
        cam.targetTexture = target;
        cam.backgroundColor = clear;
        cam.Render();
    }

    /// <summary>The matte (Resources/WmvScreenshotMatte.shader): straight colour and coverage from the render over
    /// black and the render over white, into an 8-bit target.</summary>
    static void DrawScreenshotMatte(Material matte, RenderTexture overBlack, RenderTexture overWhite, RenderTexture into)
    {
        matte.SetTexture(OverBlackId, overBlack);
        matte.SetTexture(OverWhiteId, overWhite);
        using (var cmd = new CommandBuffer { name = "WmvScreenshotMatte" })
        {
            cmd.SetRenderTarget(into);
            cmd.DrawProcedural(Matrix4x4.identity, matte, 0, MeshTopology.Triangles, 3, 1);
            Graphics.ExecuteCommandBuffer(cmd);
        }
    }

    /// <summary>One capture, synchronously, inside the end of the frame (see the file header). Never throws.</summary>
    WmvIpcClient.ScreenshotReport TakeScreenshot(WmvIpcClient.ScreenshotRequest req, int n)
    {
        var total = System.Diagnostics.Stopwatch.StartNew();
        var report = new WmvIpcClient.ScreenshotReport
        {
            Request = req.request, Path = req.path, Width = req.width, Height = req.height, Error = "",
        };
        Camera cam = Camera.main;
        if (cam == null)
        {
            report.Error = "the viewport has no camera";
            return report;
        }
        UniversalAdditionalCameraData camData = cam.GetUniversalAdditionalCameraData();
        // VALIDATION ONLY (WMV_SCREENSHOT_PASSES): the renders themselves, written beside the PNG (WriteScreenshotPasses).
        string passes = Environment.GetEnvironmentVariable("WMV_SCREENSHOT_PASSES");

        // THE LIVE STATE, recorded before anything is touched and compared once everything is put back.
        ScreenshotCameraState live = ScreenshotCameraState.Of(cam, camData);
        RenderTexture activeBefore = RenderTexture.active;
        int frameBefore = Time.frameCount;
        string sceneBefore = DescribeScreenshotScene();
        long gpuBefore = UnityEngine.Profiling.Profiler.GetAllocatedMemoryForGraphicsDriver();
        long nativeBefore = UnityEngine.Profiling.Profiler.GetTotalAllocatedMemoryLong();
        long heapBefore = GC.GetTotalMemory(false);
        int texturesBefore = Resources.FindObjectsOfTypeAll<RenderTexture>().Length;
        long gpuPeak = 0, nativePeak = 0, heapPeak = 0;
        float captureAspect = (float)req.width / req.height;
        float captureFov = CaptureFieldOfView(live.FieldOfView, live.Aspect, captureAspect);
        double blackMs = 0, whiteMs = 0, matteMs = 0, readbackMs = 0;
        string passesLine = "";

        RenderTexture overBlack = null, overWhite = null, matteTarget = null;
        Material matte = null;
        Texture2D pixels = null;
        try
        {
            Shader matteShader = Resources.Load<Shader>("WmvScreenshotMatte");
            if (matteShader == null)
                matteShader = Shader.Find("Wmv/ScreenshotMatte");
            if (matteShader == null || !matteShader.isSupported)
                throw new InvalidOperationException("the matte shader is missing from the player or not supported");
            matte = new Material(matteShader) { name = "WmvScreenshotMatte", hideFlags = HideFlags.HideAndDontSave };
            var passDesc = new RenderTextureDescriptor(req.width, req.height, RenderTextureFormat.ARGBHalf, 24) { msaaSamples = 1 };
            overBlack = NewScreenshotTexture(passDesc, "WmvScreenshotOverBlack");
            overWhite = NewScreenshotTexture(passDesc, "WmvScreenshotOverWhite");
            matteTarget = NewScreenshotTexture(new RenderTextureDescriptor(req.width, req.height, RenderTextureFormat.ARGB32, 0)
                                               {
                                                   msaaSamples = 1, sRGB = false,
                                               }, "WmvScreenshotMatte");

            cam.aspect = captureAspect;
            cam.fieldOfView = captureFov;
            cam.clearFlags = CameraClearFlags.SolidColor;
            if (camData != null)
                camData.renderPostProcessing = false;   // see the file header: no bloom in the PNG
            if (shadowRig != null)
                shadowRig.RenderFor(cam);

            // Both renders back to back: nothing runs in between that could move a clock, a pose or a particle. The
            // stage times are the CPU's; the readback waits for the GPU to finish all three passes.
            var stage = System.Diagnostics.Stopwatch.StartNew();
            RenderScreenshotPass(cam, overBlack, new Color(0f, 0f, 0f, 0f));
            blackMs = stage.Elapsed.TotalMilliseconds;
            stage.Restart();
            RenderScreenshotPass(cam, overWhite, new Color(1f, 1f, 1f, 0f));
            whiteMs = stage.Elapsed.TotalMilliseconds;
            cam.targetTexture = live.TargetTexture;

            stage.Restart();
            DrawScreenshotMatte(matte, overBlack, overWhite, matteTarget);
            matteMs = stage.Elapsed.TotalMilliseconds;

            stage.Restart();
            RenderTexture.active = matteTarget;
            pixels = NewScreenshotPixels(req.width, req.height, GraphicsFormat.R8G8B8A8_UNorm, "WmvScreenshotPixels");
            pixels.ReadPixels(new Rect(0, 0, req.width, req.height), 0, 0, false);
            RenderTexture.active = activeBefore;
            readbackMs = stage.Elapsed.TotalMilliseconds;
            report.RenderMs = blackMs + whiteMs + matteMs + readbackMs;
            gpuPeak = UnityEngine.Profiling.Profiler.GetAllocatedMemoryForGraphicsDriver();
            nativePeak = UnityEngine.Profiling.Profiler.GetTotalAllocatedMemoryLong();

            var encode = System.Diagnostics.Stopwatch.StartNew();
            byte[] png = ImageConversion.EncodeToPNG(pixels);
            report.EncodeMs = encode.Elapsed.TotalMilliseconds;
            heapPeak = GC.GetTotalMemory(false);
            nativePeak = Math.Max(nativePeak, UnityEngine.Profiling.Profiler.GetTotalAllocatedMemoryLong());
            if (png == null || png.Length == 0)
                throw new InvalidOperationException("the PNG encoder returned nothing");

            var write = System.Diagnostics.Stopwatch.StartNew();
            File.WriteAllBytes(req.path, png);
            report.WriteMs = write.Elapsed.TotalMilliseconds;
            report.Bytes = new FileInfo(req.path).Length;
            report.Ok = true;
            report.TotalMs = total.Elapsed.TotalMilliseconds;

            if (!string.IsNullOrEmpty(passes))
                passesLine = WriteScreenshotPasses(cam, req, overBlack, overWhite, live.Background);
        }
        catch (Exception e)
        {
            report.Ok = false;
            report.Error = (e is IOException || e is UnauthorizedAccessException ? "the file could not be written: " : "the capture failed: ")
                           + e.Message;
        }
        finally
        {
            // Put back everything the capture changed, in any case.
            cam.targetTexture = live.TargetTexture;
            cam.clearFlags = live.ClearFlags;
            cam.backgroundColor = live.Background;
            cam.fieldOfView = live.FieldOfView;
            cam.ResetAspect();                       // the viewport's aspect follows its window (nothing sets it)
            if (cam.aspect != live.Aspect)
                cam.aspect = live.Aspect;            // ... unless something had set it
            if (camData != null)
                camData.renderPostProcessing = live.PostProcessing;
            RenderTexture.active = activeBefore;
            if (shadowRig != null)
                shadowRig.RenderFor(cam);            // the maps and globals for the live camera again
            foreach (RenderTexture rt in new[] { overBlack, overWhite, matteTarget })
            {
                if (rt == null)
                    continue;
                rt.Release();
                DestroyImmediate(rt);
            }
            if (matte != null)
                DestroyImmediate(matte);
            if (pixels != null)
                DestroyImmediate(pixels);
        }
        if (!report.Ok)
            report.TotalMs = total.Elapsed.TotalMilliseconds;

        string restored = ScreenshotCameraState.Of(cam, camData).DifferencesFrom(live);
        if (RenderTexture.active != activeBefore)
            restored += (restored.Length > 0 ? ", " : "") + "RenderTexture.active";
        string sceneAfter = DescribeScreenshotScene();
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        string line = string.Format(inv,
            "WMV: screenshot {0} (request {1}) {2}: {3} x {4}, {5} bytes, render over black {6:F1} ms, over white {7:F1} ms, " +
            "matte {8:F1} ms, readback {9:F1} ms, PNG encode {10:F1} ms, write {11:F1} ms, total {12:F1} ms; capture field of view " +
            "{13:0.###} at aspect {14:F4}; live {15}; after the capture {16}; frame {17} -> {18}; {19}{20}; graphics driver memory " +
            "{21:F1} MB -> {22:F1} MB with the capture's textures -> {23:F1} MB after, Unity native memory {24:F1} MB -> {25:F1} MB " +
            "at the peak, managed heap {26:F1} MB -> {27:F1} MB after the encode, render textures {28} -> {29}{30}",
            n, req.request, report.Ok ? "saved to " + req.path : "FAILED: " + report.Error, req.width, req.height, report.Bytes,
            blackMs, whiteMs, matteMs, readbackMs, report.EncodeMs, report.WriteMs, report.TotalMs, captureFov, captureAspect,
            live.Describe(), restored.Length == 0 ? "everything as it was" : "DIFFERENT: " + restored, frameBefore, Time.frameCount,
            sceneBefore, sceneAfter == sceneBefore ? " (unchanged by the capture)" : " -> CHANGED: " + sceneAfter,
            gpuBefore / 1048576.0, gpuPeak / 1048576.0, UnityEngine.Profiling.Profiler.GetAllocatedMemoryForGraphicsDriver() / 1048576.0,
            nativeBefore / 1048576.0, nativePeak / 1048576.0, heapBefore / 1048576.0, heapPeak / 1048576.0, texturesBefore,
            Resources.FindObjectsOfTypeAll<RenderTexture>().Length, passesLine.Length > 0 ? "; " + passesLine : "");
        if (report.Ok && restored.Length == 0)
            Debug.Log(line);
        else
            Debug.LogWarning(line);
        return report;
    }

    /// <summary>
    /// VALIDATION ONLY (WMV_SCREENSHOT_PASSES set): the renders the matte was made from, and two more, as 8-bit PNGs
    /// beside the export, read back the way the viewport displays them (an sRGB copy of the FP16 target):
    /// &lt;name&gt;-over-black.png and -over-white.png, then -over-background.png, rendered over the viewport's own clear
    /// colour, and -over-black-again.png, rendered over black once more after the others. A run compares the PNG
    /// composited over black, white and the viewport's background with the first three, and the last with the first,
    /// to show that nothing moved between the renders. Still inside the capture: the camera is set up for it.
    /// </summary>
    string WriteScreenshotPasses(Camera cam, WmvIpcClient.ScreenshotRequest req, RenderTexture overBlack, RenderTexture overWhite,
                                 Color liveBackground)
    {
        var watch = System.Diagnostics.Stopwatch.StartNew();
        string stem = req.path.Substring(0, req.path.Length - 4);
        RenderTexture readback = null;
        Texture2D bytes = null;
        try
        {
            readback = NewScreenshotTexture(new RenderTextureDescriptor(req.width, req.height, RenderTextureFormat.ARGB32, 0)
                                            {
                                                msaaSamples = 1, sRGB = true,
                                            }, "WmvScreenshotPassReadback");
            bytes = NewScreenshotPixels(req.width, req.height, GraphicsFormat.R8G8B8A8_SRGB, "WmvScreenshotPassPixels");
            System.Action<RenderTexture, string> write = (rt, suffix) =>
            {
                Graphics.Blit(rt, readback);
                RenderTexture.active = readback;
                bytes.ReadPixels(new Rect(0, 0, req.width, req.height), 0, 0, false);
                File.WriteAllBytes(stem + suffix, ImageConversion.EncodeToPNG(bytes));
            };
            write(overBlack, "-over-black.png");
            write(overWhite, "-over-white.png");
            RenderScreenshotPass(cam, overWhite, liveBackground);
            write(overWhite, "-over-background.png");
            RenderScreenshotPass(cam, overBlack, new Color(0f, 0f, 0f, 0f));
            write(overBlack, "-over-black-again.png");
        }
        finally
        {
            RenderTexture.active = null;
            if (readback != null)
            {
                readback.Release();
                DestroyImmediate(readback);
            }
            if (bytes != null)
                DestroyImmediate(bytes);
        }
        return string.Format(System.Globalization.CultureInfo.InvariantCulture,
                             "validation: the renders written beside it as -over-black, -over-white, -over-background " +
                             "(clear ({0:F4}, {1:F4}, {2:F4})) and -over-black-again.png in {3:F1} ms",
                             liveBackground.r, liveBackground.g, liveBackground.b, watch.Elapsed.TotalMilliseconds);
    }
}
