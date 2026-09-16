// WmvMountProbe.cs
//
// -wmvMountCheck[=frames[:mountfirst]]: a character riding a mount, measured over the frames after a mount goes on or
// its clip is switched (WmvMain.StartMountProbe). DIAGNOSTICS ONLY: nothing of it exists without the switch, and it
// changes nothing it measures (with :mountfirst, only which LateUpdate the mount's pose is written from).
//
// THE ORDER OF THE LATEUPDATES. The character's body root hangs from a bone the mount's animator moves in its own
// LateUpdate, and Unity promises no order between that and the other LateUpdates that read the world transforms of what
// hangs there: the character's animator (its billboard bones take a WORLD rotation, WmvM2Animator.ApplyBillboards, and
// its emitters their billboard axes in the body root's space, WmvEmitterRuntime.BillboardBasis), the animators of what
// the character wears, and the shadow rig (its map is rendered from the transforms of that moment, WmvShadowRig).
// Positions and skinning cannot lag -- the hierarchy carries them to wherever the bone is when the frame is drawn -- so
// what a late mount costs is a facing and a shadow map one frame of mount motion behind. Each frame this records where
// every animator's LateUpdate fell (WmvM2Animator.LateUpdated), the mount's attachment bone and the character's bone
// farthest from its root as the character's animator saw them and as the frame was drawn (the main camera's
// RenderPipelineManager.beginCameraRendering), how far the body root had still to turn, and the character's bone as the
// shadow camera saw it. With :mountfirst the mount's animator is taken out of Unity's order for the window and run from
// here, before every other LateUpdate (DefaultExecutionOrder): the explicit mount-first order, to compare with.
//
// HOW FAR THE ANIMATION CARRIES THE MODELS OUT OF THE FRAMED BOX. The view is fitted once, when the mount goes on
// (WmvMain.FrameRiddenScene); each frame the character's box carried through its root, its drawn bounds and the mount's
// drawn bounds are compared with that box, and the largest excursion is reported.
//
// THE ORDER BETWEEN THE TWO MODELS' BLENDED BATCHES. After the window the view is drawn offscreen from the main camera's
// pose with the mount's transparent batches wholly before the character's and wholly after, and the same for the two
// models' emitters: the pixels that differ are the pixels whose picture depends on that order.

using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEngine;
using UnityEngine.Rendering;

[DefaultExecutionOrder(-32000)]
public class WmvMountProbe : MonoBehaviour
{
    string label;
    Action<string> log;
    WmvRuntimeModel mount, rider;
    WmvM2Animator mountAnimator, riderAnimator;
    Transform mountBone, riderRoot, riderBone;
    int riderBoneIndex = -1;
    Camera shadowCamera;
    Bounds framed;
    int frames;
    MethodInfo mountLateUpdate;       // :mountfirst -- the mount's LateUpdate, run from here
    bool stopped, windowDone;
    readonly System.Diagnostics.Stopwatch clock = new System.Diagnostics.Stopwatch();

    // This frame.
    int stamp = -1, ranks, mountRank, riderRank, partsBeforeMount, shadowAfter;
    bool riderSeen, shadowSeen, drawn;
    Vector3 mountBoneSeen, riderBoneSeen, riderBoneShadow;
    Quaternion rootSeen;

    // The window.
    int recorded, riderBeforeMount, mountBeforeRider, shadowBeforeAll, lagFrames, shadowFrames, shadowLagFrames, partsFrames;
    bool haveLast;
    Vector3 lastMountBone;
    Quaternion lastMountTurn;
    double lastDrawnMs;
    float maxStep, maxTurn, sumTurn, maxLag, sumLag, maxRootLag, sumRootLag, maxShadowLag, sumShadowLag;
    // The same motions per second of wall time (a lag is one frame's worth of them), over blocks of RateBlock frames
    // after the first.
    const int RateBlock = 20;
    float maxTurnRate, maxRootLagRate, maxLagRate;
    int blockFrames;
    float blockMs, blockTurn, blockRootLag, blockLag;
    float firstLag = -1f, firstRootLag = -1f, firstShadowLag = -1f;
    float boxOut, riderDrawnOut, mountDrawnOut;

    /// <summary>Measure the mount on screen and the character hanging from it for the next frames (see the file header).
    /// host is WmvMain's object: the shadow rig's camera is its child.</summary>
    public static WmvMountProbe Begin(GameObject host, string label, WmvRuntimeModel mount, WmvRuntimeModel rider, Bounds framed,
                                      int frames, bool mountFirst, Action<string> log)
    {
        if (host == null || mount == null || mount.Root == null || rider == null || rider.Root == null || log == null)
            return null;
        var p = host.AddComponent<WmvMountProbe>();
        p.label = label;
        p.log = log;
        p.mount = mount;
        p.rider = rider;
        p.framed = framed;
        p.frames = Math.Max(frames, 1);
        p.mountAnimator = mount.Animator;
        p.riderAnimator = rider.Animator;
        p.riderRoot = rider.Root.transform;
        p.mountBone = p.riderRoot.parent != null ? p.riderRoot.parent : mount.Root.transform;
        // The character's bone farthest from its root: the one a turn of the mount bone moves most.
        float farthest = -1f;
        for (int i = 0; i < rider.Bones.Length; i++)
        {
            if (rider.Bones[i] == null)
                continue;
            float d = Vector3.Distance(rider.Bones[i].position, p.riderRoot.position);
            if (d > farthest)
            {
                farthest = d;
                p.riderBone = rider.Bones[i];
                p.riderBoneIndex = i;
            }
        }
        if (p.riderBone == null)
            p.riderBone = p.riderRoot;
        Transform shadow = host.transform.Find("WmvShadowCamera");
        p.shadowCamera = shadow != null ? shadow.GetComponent<Camera>() : null;
        if (mountFirst && p.mountAnimator != null)
        {
            p.mountLateUpdate = typeof(WmvM2Animator).GetMethod("LateUpdate", BindingFlags.Instance | BindingFlags.NonPublic);
            if (p.mountLateUpdate != null)
                p.mountAnimator.enabled = false;             // no LateUpdate of its own: this one runs it, first
        }
        WmvM2Animator.LateUpdated += p.OnAnimator;
        RenderPipelineManager.beginCameraRendering += p.OnCamera;
        p.clock.Start();
        log(string.Format("mount check {0}: {1} frame(s), {2}; the mount's bone {3}, the character's bone {4} ({5:F2} from its root); " +
                          "the mount {6}, the character {7}; shadow camera {8}; framed box centre {9} extents {10}",
                          label, p.frames,
                          p.mountLateUpdate != null ? "the mount posed before every other LateUpdate (:mountfirst)" : "Unity's own LateUpdate order",
                          p.mountBone.name, p.riderBoneIndex, farthest,
                          p.mountAnimator != null ? "animated" : "not animated", p.riderAnimator != null ? "animated" : "not animated",
                          p.shadowCamera != null ? "found" : "not found", framed.center, framed.extents));
        return p;
    }

    /// <summary>End the measurement now: what was recorded is reported, the mount's animator gets its LateUpdate back.</summary>
    public void Stop(string reason)
    {
        if (stopped)
            return;
        if (!windowDone && recorded > 0)
            ReportWindow("cut short after " + recorded + " of " + frames + " frame(s): " + reason);
        else if (!windowDone && reason != null)
            log("mount check " + label + ": stopped before a frame was recorded -- " + reason);
        Release();
        Destroy(this);
    }

    void Release()
    {
        stopped = true;
        WmvM2Animator.LateUpdated -= OnAnimator;
        RenderPipelineManager.beginCameraRendering -= OnCamera;
        if (mountLateUpdate != null && mountAnimator != null)
            mountAnimator.enabled = true;
        mountLateUpdate = null;
    }

    void OnDestroy()
    {
        if (!stopped)
            Release();
    }

    // ---------------------------------------------------------------- per frame

    /// <summary>First of the frame's LateUpdates (DefaultExecutionOrder).</summary>
    void LateUpdate()
    {
        if (stopped)
            return;
        if (windowDone)
        {
            // Outside any camera's render, which the offscreen draws must be.
            ReportBlendOrder();
            Release();
            Destroy(this);
            return;
        }
        stamp = Time.frameCount;
        ranks = 0;
        mountRank = riderRank = shadowAfter = -1;
        partsBeforeMount = 0;
        riderSeen = shadowSeen = drawn = false;
        if (mountLateUpdate != null && mountAnimator != null)
            mountLateUpdate.Invoke(mountAnimator, null);
    }

    void OnAnimator(WmvM2Animator a)
    {
        if (stopped || stamp != Time.frameCount || drawn || a == null)
            return;
        ranks++;
        if (a == mountAnimator)
        {
            mountRank = ranks;
            return;
        }
        if (a == riderAnimator)
        {
            riderRank = ranks;
            if (mountBone != null && riderBone != null && riderRoot != null)
            {
                riderSeen = true;
                mountBoneSeen = mountBone.position;
                riderBoneSeen = riderBone.position;
                rootSeen = riderRoot.rotation;
            }
            return;
        }
        if (mountAnimator != null && riderRoot != null && mountRank < 0 && a.transform.IsChildOf(riderRoot))
            partsBeforeMount++;              // something the character wears, posed before the mount
    }

    void OnCamera(ScriptableRenderContext context, Camera cam)
    {
        if (stopped || stamp != Time.frameCount || cam == null)
            return;
        if (cam == shadowCamera)
        {
            if (!shadowSeen && riderBone != null)
            {
                shadowSeen = true;
                shadowAfter = ranks;
                riderBoneShadow = riderBone.position;
            }
            return;
        }
        if (!drawn && cam == Camera.main)
        {
            drawn = true;
            EndFrame();
        }
    }

    void EndFrame()
    {
        if (mount == null || mount.Root == null || rider == null || rider.Root == null || mountBone == null || riderBone == null)
        {
            Stop("the mount or the character went away");
            return;
        }
        Vector3 mb = mountBone.position, rb = riderBone.position;
        Quaternion mr = mountBone.rotation, rr = riderRoot.rotation;
        double nowMs = clock.Elapsed.TotalMilliseconds;
        float frameMs = haveLast ? (float)(nowMs - lastDrawnMs) : 0f;
        lastDrawnMs = nowMs;
        float step = haveLast ? Vector3.Distance(mb, lastMountBone) : 0f;
        float turn = haveLast ? AngleDeg(mr, lastMountTurn) : 0f;
        bool rated = haveLast && frameMs > 0.01f;
        haveLast = true;
        lastMountBone = mb;
        lastMountTurn = mr;
        float lag = riderSeen ? Vector3.Distance(rb, riderBoneSeen) : 0f;
        float rootLag = riderSeen ? AngleDeg(rr, rootSeen) : 0f;
        float mountLag = riderSeen ? Vector3.Distance(mb, mountBoneSeen) : 0f;
        float shadowLag = shadowSeen ? Vector3.Distance(rb, riderBoneShadow) : 0f;
        if (rated)
        {
            // Over blocks of frames: a single frame's time is too uneven a divisor at hundreds of frames a second.
            blockMs += frameMs;
            blockTurn += turn;
            blockRootLag += riderSeen ? rootLag : 0f;
            blockLag += riderSeen ? lag : 0f;
            if (++blockFrames == RateBlock)
            {
                maxTurnRate = Mathf.Max(maxTurnRate, blockTurn * 1000f / blockMs);
                maxRootLagRate = Mathf.Max(maxRootLagRate, blockRootLag * 1000f / blockMs);
                maxLagRate = Mathf.Max(maxLagRate, blockLag * 1000f / blockMs);
                blockFrames = 0;
                blockMs = blockTurn = blockRootLag = blockLag = 0f;
            }
        }

        // How far past the framed box: the character's box carried through its root (what the framing measured), and
        // what each renderer reports it drew.
        bool any = false;
        Vector3 mn = Vector3.zero, mx = Vector3.zero;
        WmvMountedScene.Encapsulate(rider.Bounds, riderRoot.localToWorldMatrix, ref any, ref mn, ref mx);
        boxOut = Mathf.Max(boxOut, Outside(mn, mx));
        if (rider.Skin != null)
            riderDrawnOut = Mathf.Max(riderDrawnOut, Outside(rider.Skin.bounds.min, rider.Skin.bounds.max));
        if (mount.Skin != null)
            mountDrawnOut = Mathf.Max(mountDrawnOut, Outside(mount.Skin.bounds.min, mount.Skin.bounds.max));

        recorded++;
        if (recorded > 1)
        {
            maxStep = Mathf.Max(maxStep, step);
            maxTurn = Mathf.Max(maxTurn, turn);
            sumTurn += turn;
        }
        if (riderRank > 0 && mountRank > 0)
        {
            if (riderRank < mountRank) riderBeforeMount++; else mountBeforeRider++;
        }
        // The first frame is kept apart: a mount that has just gone on still stands in the pose its build left until its
        // own first LateUpdate, which is a jump, not a frame of motion.
        if (recorded == 1)
        {
            firstLag = riderSeen ? lag : -1f;
            firstRootLag = riderSeen ? rootLag : -1f;
            firstShadowLag = shadowSeen ? shadowLag : -1f;
        }
        else if (riderSeen)
        {
            lagFrames++;
            maxLag = Mathf.Max(maxLag, lag);
            sumLag += lag;
            maxRootLag = Mathf.Max(maxRootLag, rootLag);
            sumRootLag += rootLag;
        }
        if (shadowSeen)
        {
            shadowFrames++;
            if (shadowAfter == 0) shadowBeforeAll++;
            if (recorded > 1)
            {
                shadowLagFrames++;
                maxShadowLag = Mathf.Max(maxShadowLag, shadowLag);
                sumShadowLag += shadowLag;
            }
        }
        if (partsBeforeMount > 0)
            partsFrames++;

        log(string.Format(
            "mount check {0} frame {1}/{2} ({3:F2} ms): LateUpdate order mount #{4}, character #{5}, {6} part(s) of the character " +
            "before the mount, shadow map after {7} animator(s) | mount sequence {8} at {9:F0} ms, bone at ({10:F4}, {11:F4}, {12:F4}) " +
            "moved {13:F5} and turned {14:F4} deg since the frame before | character bone {15} at ({16:F4}, {17:F4}, {18:F4}): the " +
            "character's animator saw it {19:F5} and the mount bone {20:F5} from where they were drawn, the body root {21:F4} deg " +
            "short of its turn; the shadow map saw the bone {22}",
            label, recorded, frames, frameMs, mountRank, riderRank, partsBeforeMount, shadowSeen ? shadowAfter.ToString() : "- (not seen)",
            mountAnimator != null ? mountAnimator.SequenceIndex : -1, mountAnimator != null ? mountAnimator.TimeMs : 0.0,
            mb.x, mb.y, mb.z, step, turn, riderBoneIndex, rb.x, rb.y, rb.z,
            riderSeen ? lag : -1f, riderSeen ? mountLag : -1f, riderSeen ? rootLag : -1f,
            shadowSeen ? shadowLag.ToString("F5") + " from where it was drawn" : "- (the shadow camera did not render)"));

        if (recorded >= frames)
        {
            ReportWindow(null);
            windowDone = true;
            // Stop listening now; the blend check runs at the next LateUpdate, outside this camera's render.
            WmvM2Animator.LateUpdated -= OnAnimator;
            RenderPipelineManager.beginCameraRendering -= OnCamera;
            if (mountLateUpdate != null && mountAnimator != null)
                mountAnimator.enabled = true;
            mountLateUpdate = null;
        }
    }

    /// <summary>The largest distance a box (min, max) reaches past the framed box on any side, 0 inside it.</summary>
    float Outside(Vector3 min, Vector3 max)
    {
        Vector3 fmin = framed.min, fmax = framed.max;
        float o = 0f;
        o = Mathf.Max(o, fmin.x - min.x);
        o = Mathf.Max(o, fmin.y - min.y);
        o = Mathf.Max(o, fmin.z - min.z);
        o = Mathf.Max(o, max.x - fmax.x);
        o = Mathf.Max(o, max.y - fmax.y);
        o = Mathf.Max(o, max.z - fmax.z);
        return o;
    }

    void ReportWindow(string cut)
    {
        float radius = Mathf.Max(framed.extents.magnitude, 1e-4f);
        log(string.Format(
            "mount check {0}: {1} frame(s) in {2} ms{3} -- the character's animator ran before the mount's in {4} frame(s) and after it " +
            "in {5}; in the first frame what it saw lagged the drawn frame by {6:F5} at its bone {7} and {8:F4} deg at its body root " +
            "(the shadow map {9:F5}); over the frames after it, by at most {10:F5} (mean {11:F5}) and {12:F4} deg (mean {13:F4}); the " +
            "mount bone moved at most {14:F5} and turned at most {15:F4} deg (mean {16:F4}) per frame; parts of the character were " +
            "posed before the mount in {17} frame(s); the shadow map was rendered before any animator in {18} of {19} frame(s) and, " +
            "after the first, saw the character's bone at most {20:F5} (mean {21:F5}) from where it was drawn; past the framed box " +
            "(radius {22:F3}): the character's box {23:F4}, its drawn bounds {24:F4}, the mount's drawn bounds {25:F4}",
            label, recorded, clock.ElapsedMilliseconds, cut != null ? " (" + cut + ")" : "", riderBeforeMount, mountBeforeRider,
            firstLag, riderBoneIndex, firstRootLag, firstShadowLag,
            maxLag, lagFrames > 0 ? sumLag / lagFrames : 0f, maxRootLag, lagFrames > 0 ? sumRootLag / lagFrames : 0f,
            maxStep, maxTurn, recorded > 1 ? sumTurn / (recorded - 1) : 0f, partsFrames, shadowBeforeAll, shadowFrames,
            maxShadowLag, shadowLagFrames > 0 ? sumShadowLag / shadowLagFrames : 0f, radius, boxOut, riderDrawnOut, mountDrawnOut));
        // A lag is one frame of motion, so how large it is depends on the frame rate: per second of wall time (the largest over
        // blocks of RateBlock frames after the first), and what one frame of that is at 60 frames a second.
        log(string.Format(
            "mount check {0}: as rates over {1}-frame blocks -- the mount bone turned at most {2:F2} deg/s, the character's body root " +
            "was left behind at up to {3:F2} deg/s and its bone {4} at up to {5:F4} units/s; one frame of that at 60 frames a second " +
            "is {6:F3} deg, {7:F3} deg and {8:F5} units",
            label, RateBlock, maxTurnRate, maxRootLagRate, riderBoneIndex, maxLagRate, maxTurnRate / 60f, maxRootLagRate / 60f,
            maxLagRate / 60f));
    }

    /// <summary>The angle between two rotations in degrees, exact for small ones: Quaternion.Angle reads anything under
    /// about 0.16 degrees as 0, and one frame of a mount's turn is often less.</summary>
    static float AngleDeg(Quaternion a, Quaternion b)
    {
        // conj(a) * b, in double: its vector part is sin(angle / 2) along the axis, its scalar part cos(angle / 2).
        double aw = a.w, ax = a.x, ay = a.y, az = a.z, bw = b.w, bx = b.x, by = b.y, bz = b.z;
        double w = aw * bw + ax * bx + ay * by + az * bz;
        double x = aw * bx - bw * ax - (ay * bz - az * by);
        double y = aw * by - bw * ay - (az * bx - ax * bz);
        double z = aw * bz - bw * az - (ax * by - ay * bx);
        return (float)(2.0 * Math.Atan2(Math.Sqrt(x * x + y * y + z * z), Math.Abs(w)) * 180.0 / Math.PI);
    }

    // ---------------------------------------------------------------- the blended batches of the two models

    void ReportBlendOrder()
    {
        Camera view = Camera.main;
        if (view == null || mount == null || mount.Root == null || rider == null || rider.Root == null)
        {
            log("mount check " + label + ": blend order not measured -- no camera, or the mount or the character went away");
            return;
        }
        var mountBatches = new List<Material>();
        var mountEmitters = new List<Material>();
        var riderBatches = new List<Material>();
        var riderEmitters = new List<Material>();
        Collect(mount.Root.transform, riderRoot, mountBatches, mountEmitters);
        Collect(riderRoot, null, riderBatches, riderEmitters);
        string counts = string.Format("the mount has {0} blended batch material(s) and {1} emitter material(s), the character with " +
                                      "what it wears {2} and {3}", mountBatches.Count, mountEmitters.Count, riderBatches.Count,
                                      riderEmitters.Count);
        if ((mountBatches.Count + mountEmitters.Count) == 0 || (riderBatches.Count + riderEmitters.Count) == 0)
        {
            log("mount check " + label + ": blend order -- " + counts + ": nothing of one model to order against the other");
            return;
        }

        var original = new Dictionary<Material, int>();
        foreach (var list in new[] { mountBatches, mountEmitters, riderBatches, riderEmitters })
            foreach (Material m in list)
                original[m] = m.renderQueue;

        int w = Mathf.Clamp(view.pixelWidth, 64, 2048), h = Mathf.Clamp(view.pixelHeight, 64, 2048);
        var go = new GameObject("WmvMountProbeCamera");
        Camera cam = go.AddComponent<Camera>();
        cam.CopyFrom(view);
        cam.enabled = false;                                 // rendered by hand, below
        cam.targetTexture = null;
        RenderTexture rt = RenderTexture.GetTemporary(w, h, 24, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Default);
        RenderTexture prevActive = RenderTexture.active;
        try
        {
            Color32[] asDrawn = Grab(cam, rt, w, h);
            // Batches: one model's blended batches wholly after the other's, every emitter after both.
            Offset(riderBatches, 1000); Place(mountEmitters, 4950); Place(riderEmitters, 4950);
            Color32[] mountBatchesFirst = Grab(cam, rt, w, h);
            Restore(original);
            Offset(mountBatches, 1000); Place(mountEmitters, 4950); Place(riderEmitters, 4950);
            Color32[] riderBatchesFirst = Grab(cam, rt, w, h);
            Restore(original);
            // Emitters: one model's after the other's, the batches as they are.
            Offset(riderEmitters, 1);
            Color32[] mountEmittersFirst = Grab(cam, rt, w, h);
            Restore(original);
            Offset(mountEmitters, 1);
            Color32[] riderEmittersFirst = Grab(cam, rt, w, h);
            Restore(original);
            // Controls: each model's transparent materials drawn before every opaque batch -- a change a frame cannot hide
            // wherever they are seen, so a 0 above says nothing overlaps rather than that the check sees nothing.
            Place(riderBatches, 1999); Place(riderEmitters, 1999);
            Color32[] riderControl = Grab(cam, rt, w, h);
            Restore(original);
            Place(mountBatches, 1999); Place(mountEmitters, 1999);
            Color32[] mountControl = Grab(cam, rt, w, h);
            Restore(original);

            string batchBox, emitterBox, drawnBox, ignored;
            int batchDiff = Differ(mountBatchesFirst, riderBatchesFirst, w, out batchBox);
            int emitterDiff = Differ(mountEmittersFirst, riderEmittersFirst, w, out emitterBox);
            int drawnVsMountFirst = Differ(asDrawn, mountBatchesFirst, w, out drawnBox);
            int drawnVsRiderFirst = Differ(asDrawn, riderBatchesFirst, w, out ignored);
            int riderControlDiff = Differ(asDrawn, riderControl, w, out ignored);
            int mountControlDiff = Differ(asDrawn, mountControl, w, out ignored);
            log(string.Format(
                "mount check {0}: blend order -- {1}; drawn {2}x{3} from the viewport's pose: the mount's blended batches wholly " +
                "before the character's and wholly after differ in {4} pixel(s){5}; the two models' emitters one after the other " +
                "and the other way round differ in {6} pixel(s){7}; the frame as drawn differs from the mount-first batches in {8} " +
                "pixel(s){9} and from the character-first in {10}; controls -- the character's transparent materials drawn before " +
                "the opaque ones change {11} pixel(s), the mount's {12}",
                label, counts, w, h, batchDiff, batchBox, emitterDiff, emitterBox, drawnVsMountFirst, drawnBox, drawnVsRiderFirst,
                riderControlDiff, mountControlDiff));
            if (WmvModelBuilder.Debug_.LightDump && (batchDiff > 0 || emitterDiff > 0))
            {
                Dump(asDrawn, w, h, "wmv-mountcheck-drawn.png");
                Dump(mountBatchesFirst, w, h, "wmv-mountcheck-batches-mount-first.png");
                Dump(riderBatchesFirst, w, h, "wmv-mountcheck-batches-character-first.png");
                Dump(mountEmittersFirst, w, h, "wmv-mountcheck-emitters-mount-first.png");
                Dump(riderEmittersFirst, w, h, "wmv-mountcheck-emitters-character-first.png");
            }
        }
        catch (Exception e)
        {
            log("mount check " + label + ": blend order could not be drawn: " + e.GetType().Name + ": " + e.Message);
        }
        finally
        {
            Restore(original);
            RenderTexture.active = prevActive;
            cam.targetTexture = null;
            RenderTexture.ReleaseTemporary(rt);
            Destroy(go);
        }
    }

    /// <summary>The transparent materials under a root -- blended batches (queues 3000..3899) and emitters (3900 and up) --
    /// leaving out a subtree (the character, under the mount's bone).</summary>
    static void Collect(Transform root, Transform leaveOut, List<Material> batches, List<Material> emitters)
    {
        foreach (Renderer r in root.GetComponentsInChildren<Renderer>(true))
        {
            if (leaveOut != null && r.transform.IsChildOf(leaveOut))
                continue;
            foreach (Material m in r.sharedMaterials)
            {
                if (m == null || m.renderQueue < (int)RenderQueue.Transparent)
                    continue;
                List<Material> into = m.renderQueue >= (int)RenderQueue.Transparent + 900 ? emitters : batches;
                if (!into.Contains(m))
                    into.Add(m);
            }
        }
    }

    static void Offset(List<Material> materials, int by)
    {
        foreach (Material m in materials) m.renderQueue += by;
    }

    static void Place(List<Material> materials, int queue)
    {
        foreach (Material m in materials) m.renderQueue = queue;
    }

    static void Restore(Dictionary<Material, int> original)
    {
        foreach (var kv in original)
            if (kv.Key != null) kv.Key.renderQueue = kv.Value;
    }

    static Color32[] Grab(Camera cam, RenderTexture rt, int w, int h)
    {
        cam.targetTexture = rt;
        cam.Render();
        cam.targetTexture = null;
        RenderTexture prev = RenderTexture.active;
        RenderTexture.active = rt;
        var tex = new Texture2D(w, h, TextureFormat.RGBA32, false);
        tex.ReadPixels(new Rect(0, 0, w, h), 0, 0);
        tex.Apply(false);
        RenderTexture.active = prev;
        Color32[] px = tex.GetPixels32();
        Destroy(tex);
        return px;
    }

    /// <summary>Pixels where two frames differ by more than 2 in a channel, and the rectangle they span.</summary>
    static int Differ(Color32[] a, Color32[] b, int w, out string box)
    {
        int n = 0, x0 = int.MaxValue, y0 = int.MaxValue, x1 = -1, y1 = -1;
        for (int i = 0; i < a.Length && i < b.Length; i++)
        {
            if (Math.Abs(a[i].r - b[i].r) <= 2 && Math.Abs(a[i].g - b[i].g) <= 2 && Math.Abs(a[i].b - b[i].b) <= 2)
                continue;
            n++;
            int x = i % w, y = i / w;
            x0 = Math.Min(x0, x); y0 = Math.Min(y0, y); x1 = Math.Max(x1, x); y1 = Math.Max(y1, y);
        }
        box = n > 0 ? string.Format(" (x {0}..{1}, y {2}..{3}, rows bottom-up)", x0, x1, y0, y1) : "";
        return n;
    }

    void Dump(Color32[] px, int w, int h, string name)
    {
        try
        {
            var tex = new Texture2D(w, h, TextureFormat.RGBA32, false);
            tex.SetPixels32(px);
            tex.Apply(false);
            string path = System.IO.Path.Combine(Application.dataPath, "../" + name);
            System.IO.File.WriteAllBytes(path, ImageConversion.EncodeToPNG(tex));
            Destroy(tex);
            log("mount check " + label + ": wrote " + path);
        }
        catch (Exception e)
        {
            log("mount check " + label + ": could not write " + name + ": " + e.Message);
        }
    }
}
