// WmvLifecycleSelfTest.cs
//
// The material-animator LIFECYCLE, exercised through the real runtime (-wmvLifecycleTest): build a
// synthetic model whose only material input is a texture transform with no keys in the idle and
// keys in another sequence, then drive it through sequence changes the way the host does and
// check what exists and what the material holds at each step:
//
//   built in sequence 0  -> the animator exists but is dormant (nothing evaluated), no clock,
//                           identity UV
//   sequence 1           -> one binding evaluated, a clock exists, the UV transform is applied
//   back to sequence 0   -> dormant again, no clock, identity restored
//   sequence 1 again     -> active again
//
// and the reverse shape (keys in 0, none in 1), for a static mesh and for a skinned model whose
// selected sequence moves no bone. No model is rebuilt at any step. The result lines are what
// the headless regression greps for.

using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEngine;
using Wmv.Wow;
using Wmv.Wow.World;

public static partial class WmvLifecycleSelfTest
{
    static int passed, failed;

    static void Check(bool ok, string what, Action<string> log)
    {
        if (ok) passed++; else failed++;
        log((ok ? "lifecycle-test PASS: " : "lifecycle-test FAIL: ") + what);
    }

    static bool Near(float a, float b) { return Mathf.Abs(a - b) < 1e-4f; }

    static bool UvIs(WmvRuntimeModel rt, float m00, float m01, float m10, float m11, float ox, float oy)
    {
        if (rt.Materials.Length == 0 || rt.Materials[0] == null) return false;
        Vector4 xf = rt.Materials[0].GetVector("_UvXf0");
        Vector4 off = rt.Materials[0].GetVector("_UvOff0");
        return Near(xf.x, m00) && Near(xf.y, m01) && Near(xf.z, m10) && Near(xf.w, m11) &&
               Near(off.x, ox) && Near(off.y, oy);
    }

    static bool Identity(WmvRuntimeModel rt) { return UvIs(rt, 1f, 0f, 0f, 1f, 0f, 0f); }

    /// <summary>The keyed transform as the shader receives it: scale (2,2), translation (0.25,0.5)
    /// conjugated by the V flip -- see WmvMaterialAnimator.UvVectors.</summary>
    static bool Applied(WmvRuntimeModel rt)
    {
        float sx = M2Synthetic.KeyedSx, sy = M2Synthetic.KeyedSy, tx = M2Synthetic.KeyedTx, ty = M2Synthetic.KeyedTy;
        return UvIs(rt, sx, 0f, 0f, sy, tx, 1f - sy - ty);
    }

    static void Step(WmvRuntimeModel rt, M2ParsedModel model, byte[] m2, int seq, bool expectActive,
                     string label, Action<string> log)
    {
        M2Parser.ReadAnimationInto(m2, seq, model);
        bool ok = WmvModelBuilder.ApplySequence(rt, model, log);
        int count = rt.MaterialAnimator != null ? rt.MaterialAnimator.AnimatedCount : -1;
        Check(rt.MaterialAnimator != null, label + ": material animator still exists", log);
        Check(count == (expectActive ? 1 : 0), label + ": bindings evaluated per frame = " + count, log);
        Check((rt.Animator != null) == expectActive, label + (expectActive ? ": a clock exists" : ": no clock (nothing needs one)"), log);
        if (expectActive)
        {
            Check(ok, label + ": ApplySequence reports something to animate", log);
            if (rt.MaterialAnimator != null) rt.MaterialAnimator.Apply(0f);
            Check(Applied(rt), label + ": UV transform applied to the material", log);
        }
        else
            Check(Identity(rt), label + ": identity UV restored", log);
    }

    static void Run(bool skinned, int keyed, Action<string> log)
    {
        string v = (skinned ? "skinned (no bone moves)" : "static mesh") + ", keys in sequence " + keyed;
        byte[] m2 = M2Synthetic.TransformSwitchModel(keyed, skinned);
        byte[] sk = M2Synthetic.TransformSwitchSkin();
        M2ParsedModel model = M2Parser.Parse(m2, 0);
        M2ParsedSkin skin = M2SkinParser.Parse(sk);
        WmvRuntimeModel rt = WmvModelBuilder.Build(model, skin, new Dictionary<int, BlpImage>(), "LifecycleTest", log);
        Check(rt != null && rt.Root != null, v + ": built", log);
        if (rt == null) return;
        Check(rt.Skinned == skinned, v + ": built through the " + (skinned ? "skinned" : "static") + " path", log);
        Check(rt.MaterialAnimator != null, v + ": material animator exists at build", log);
        bool activeAtBuild = keyed == 0;                     // built in sequence 0
        int count = rt.MaterialAnimator != null ? rt.MaterialAnimator.AnimatedCount : -1;
        Check(count == (activeAtBuild ? 1 : 0), v + ": bindings evaluated at build = " + count, log);
        Check((rt.Animator != null) == activeAtBuild, v + (activeAtBuild ? ": clock at build" : ": no clock at build"), log);
        Check(activeAtBuild ? Applied(rt) : Identity(rt), v + (activeAtBuild ? ": UV applied at build" : ": identity UV at build"), log);
        // the switches always go 1 -> 0 -> 1; which of them is the keyed one depends on the shape
        Step(rt, model, m2, 1, keyed == 1, v + " -> sequence 1", log);
        Step(rt, model, m2, 0, keyed == 0, v + " -> sequence 0", log);
        Step(rt, model, m2, 1, keyed == 1, v + " -> sequence 1 again", log);
        rt.Dispose();
    }

    static int DrawnCount(WmvRuntimeModel rt)
    {
        int n = 0;
        for (int i = 0; i < rt.SubmeshIndices.Length; i++)
            if (WmvModelBuilder.GeosetVisibleFor(rt, i)) n++;
        return n;
    }

    /// <summary>
    /// WHICH SUBMESHES A GEOSET SELECTION DRAWS, and what changing it costs.
    ///
    /// Two things are checked that nothing checked before. First the rule itself: submesh id 0 is
    /// always drawn, any other id only when the reported selection names it, and no selection at
    /// all means id 0 alone -- which is why a component opened as a raw model shows less than the
    /// same component shown as an item. Second, and the reason this lives in the runtime suite
    /// rather than the parser one: changing the selection must not rebuild anything. A geoset
    /// change is an index-buffer switch; if it started recreating the mesh, materials or textures
    /// the model would flicker and every per-model diagnostic would reset.
    /// </summary>

    // ---------------------------------------------------------------- emitters

    /// <summary>A 2x2 opaque white texture, so an emitter has something to bind. The lifecycle
    /// test never looks at a pixel; it only needs the slot to resolve.</summary>
    static BlpImage FlatTexture()
    {
        var img = new BlpImage { Width = 2, Height = 2, Rgba = new byte[2 * 2 * 4], Encoding = "test" };
        for (int i = 0; i < img.Rgba.Length; i++) img.Rgba[i] = 255;
        return img;
    }

    static Dictionary<int, BlpImage> EmitterTextures()
    {
        return new Dictionary<int, BlpImage> { { 0, FlatTexture() }, { 1, FlatTexture() } };
    }

    /// <summary>
    /// The emitter runtime, driven through the real builder the way the host drives it.
    ///
    /// What this is actually for: the emitters are the first thing in this renderer whose state
    /// is a HISTORY rather than a function of the current instant, so the questions worth asking
    /// are the lifecycle ones -- does a sequence change stop an emitter that should stop, does it
    /// leave the previous animation's particles behind, does a model without emitters pay for the
    /// feature at all, and does a rebuild produce two of everything.
    /// </summary>
    static void EmitterTests(Action<string> log)
    {
        // Rate keys in ANIMATION 0. The emitter float tracks are read there whichever sequence
        // plays -- WMV's bZeroParticle default, see M2Parser -- so this is the fixture that
        // emits, and the sequence-change check below is that switching does NOT stop it.
        byte[] m2 = M2Synthetic.EmitterModel(0);
        byte[] sk = M2Synthetic.TransformSwitchSkin();

        M2ParsedModel model = M2Parser.Parse(m2, 0);
        M2ParsedSkin skin = M2SkinParser.Parse(sk);
        WmvRuntimeModel rt = WmvModelBuilder.Build(model, skin, EmitterTextures(), "EmitterTest",
                                                   s => log("  " + s));
        Check(rt != null && rt.Root != null, "emitter: model built", log);
        Check(rt.Emitters != null, "emitter: runtime component added", log);
        if (rt.Emitters == null) { rt.Dispose(); return; }

        Check(rt.Emitters.ParticleEmitterCount == 1, "emitter: one particle emitter drawn", log);
        Check(rt.Emitters.RibbonEmitterCount == 1, "emitter: one ribbon emitter drawn", log);
        Check(rt.Emitters.DrawCallCount == 2, "emitter: two draw calls, not two per particle", log);
        Check(rt.Emitters.SkippedEmitterCount == 0, "emitter: nothing skipped", log);
        Check(rt.Animator != null, "emitter: a clock exists for the emitters to run on", log);
        Check(rt.Animator.Emitters == rt.Emitters, "emitter: the animator drives THIS runtime", log);

        // The emitter GameObjects must be children of the model root, or a model switch would
        // leave the previous model's effects hanging in the scene.
        int children = 0;
        for (int i = 0; i < rt.Root.transform.childCount; i++)
            if (rt.Root.transform.GetChild(i).GetComponent<MeshRenderer>() != null)
                children++;
        Check(children == 2, "emitter: both renderers are children of the model root", log);

        // Two seconds of animation: the emitter should be running and the ribbon laying edges.
        rt.Emitters.ResetState();
        for (int i = 0; i < 120; i++)
            rt.Emitters.Advance(1f / 60f, i * 16f, i * 16f);
        int live = rt.Emitters.LiveParticleCount;
        Check(live > 0, "emitter: the emitter emits (" + live + " live)", log);
        Check(rt.Emitters.RibbonSegmentCount > 1,
              "emitter: the ribbon laid down segments (" + rt.Emitters.RibbonSegmentCount + ")", log);

        // A sequence change, through exactly the call the host makes.
        M2Parser.ReadAnimationInto(m2, 1, model);
        bool applied = WmvModelBuilder.ApplySequence(rt, model, s => log("  " + s));
        Check(applied, "emitter: ApplySequence reports something to animate", log);
        Check(rt.Emitters.ParticleEmitterCount == 1 && rt.Emitters.RibbonEmitterCount == 1,
              "emitter: the sequence change did NOT duplicate the emitters", log);
        // THE TWO HALVES OF A SEQUENCE CHANGE PART COMPANY HERE, and the reason is in
        // WmvEmitterRuntime.Rebind. A ribbon is a trail of segments left behind by a bone, so its
        // history has to go or the first frame of the new animation draws an edge from wherever
        // the bone stood in the old one -- a smear across the model that no bone ever traced. A
        // particle owes the previous frame's bone nothing: it is a free body with its own
        // position, velocity and remaining life, and in the game changing animation does not put
        // a torch out. The legacy viewport holds one std::list<Particle> for the life of the model
        // (particle.h:74) and WoWModel::animate never touches it (WoWModel.cpp:2210-2224).
        Check(rt.Emitters.LiveParticleCount == live,
              "emitter: the sequence change KEPT the live particles (" + live + " before, "
              + rt.Emitters.LiveParticleCount + " after)", log);
        Check(rt.Emitters.RibbonSegmentCount == 0,
              "emitter: the sequence change cleared the ribbon history (no smear from where the "
              + "bone used to be)", log);

        for (int i = 0; i < 120; i++)
            rt.Emitters.Advance(1f / 60f, i * 16f, i * 16f);
        Check(rt.Emitters.LiveParticleCount > 0,
              "emitter: still emitting after the sequence change ("
              + rt.Emitters.LiveParticleCount + " live)", log);

        // A PAUSED model does not emit: dt is the animation's advance, and the animator passes
        // zero while paused. Running with dt = 0 must change nothing at all.
        int before = rt.Emitters.LiveParticleCount;
        int segsBefore = rt.Emitters.RibbonSegmentCount;
        for (int i = 0; i < 60; i++)
            rt.Emitters.Advance(0f, 500f, 500f);
        Check(rt.Emitters.LiveParticleCount == before,
              "emitter: dt = 0 (paused) spawns nothing and kills nothing", log);
        Check(rt.Emitters.RibbonSegmentCount == segsBefore,
              "emitter: dt = 0 (paused) lays down no ribbon segment", log);

        // Deterministic: the same instant, reached the same way, twice.
        rt.Emitters.SimulateTo(1000f, x => x, null);
        int a = rt.Emitters.LiveParticleCount;
        rt.Emitters.SimulateTo(1000f, x => x, null);
        Check(rt.Emitters.LiveParticleCount == a,
              "emitter: SimulateTo is deterministic (" + a + " live both times)", log);

        // A ParticleColor override must not change how many particles exist, only their colour.
        var set = new Color[3];
        for (int i = 0; i < 3; i++) set[i] = new Color(1f, 0f, 0f, 1f);
        rt.Emitters.SetParticleColorOverride(new[] { set, set, set });
        Check(rt.Emitters.LiveParticleCount == a, "emitter: a colour override changes no counts", log);
        rt.Emitters.SetParticleColorOverride(null);

        // Disposing the model drops the whole record, emitters included: the renderers are
        // children of Root, so destroying Root destroys them, and the component's own OnDestroy
        // releases the meshes and materials it made. That is why a model switch cannot leave the
        // previous model's effects on screen.
        rt.Dispose();
        Check(rt.Root == null && rt.Emitters == null,
              "emitter: disposing the model takes the emitters with it", log);

        // A model with NO emitters gets no component, no GameObject and no per-frame call. That
        // is the "costs nothing" property, and it is structural rather than a fast path.
        M2ParsedModel plain = M2Parser.Parse(M2Synthetic.TransformSwitchModel(1, true), 0);
        WmvRuntimeModel prt = WmvModelBuilder.Build(plain, skin, EmitterTextures(), "NoEmitterTest",
                                                    s => log("  " + s));
        Check(prt.Emitters == null,
              "emitter: a model with no emitters gets no runtime component at all", log);
        Check(prt.Root.GetComponent<WmvEmitterRuntime>() == null,
              "emitter: ... and none on its root either", log);
        prt.Dispose();
    }


    // ---------------------------------------------------------------- viewport zoom

    /// <summary>
    /// The four shapes the viewport actually has to serve, by bounding radius. These are real
    /// numbers from this client: an item component is a fraction of a unit, a creature is a
    /// couple of units, a large creature ten or so, and a doodad or WMO can be hundreds.
    /// </summary>
    static readonly float[] ZoomRadii = { 0.15f, 1.2f, 12f, 250f };
    static readonly string[] ZoomNames = { "item component", "normal creature", "large model",
                                           "very large model" };

    static WmvOrbitCamera NewCamera(out GameObject go)
    {
        go = new GameObject("ZoomTestCamera");
        var cam = go.AddComponent<Camera>();
        cam.fieldOfView = 60f;
        return go.AddComponent<WmvOrbitCamera>();
    }

    /// <summary>
    /// Mouse-wheel zoom, driven through the same two methods the wheel drives.
    ///
    /// The wheel itself cannot be synthesised from in-process, so what is checked here is
    /// everything downstream of the notch: that a notch moves the camera the right way by the
    /// right proportion, that the range is derived from the model rather than fixed, that the
    /// camera cannot reach or pass the focus point, and that the angle and the target survive.
    /// Whether a notch ARRIVES is a windowing question and was settled by rolling a real wheel
    /// over the viewport -- see the wheel note at the top of WmvOrbitCamera.
    /// </summary>
    static void ZoomTests(Action<string> log)
    {
        for (int i = 0; i < ZoomRadii.Length; i++)
        {
            float radius = ZoomRadii[i];
            string what = ZoomNames[i] + " (r=" + radius + ")";
            GameObject go;
            WmvOrbitCamera cam = NewCamera(out go);

            var pivot = new Vector3(3f, -2f, 5f);       // deliberately not the origin
            cam.Frame(new Bounds(pivot, Vector3.one * (radius * 2f / Mathf.Sqrt(3f))));

            float framed = cam.FramedDistance;
            Check(framed > 0f, "zoom " + what + ": framed at a positive distance", log);
            Check(cam.distance == framed, "zoom " + what + ": starts settled at the framing distance", log);

            // THE RANGE COMES FROM THE MODEL. A fixed range cannot serve r=0.15 and r=250 both.
            Check(cam.MinDistance < framed && cam.MaxDistance > framed,
                  "zoom " + what + ": framing distance sits inside the range", log);
            Check(cam.MinDistance > 0f, "zoom " + what + ": minimum is strictly positive", log);
            Check(Mathf.Abs(cam.MinDistance / framed - 0.02f) < 1e-3f,
                  "zoom " + what + ": minimum is 2% of framing, not a constant", log);
            Check(Mathf.Abs(cam.MaxDistance / framed - 20f) < 1e-2f,
                  "zoom " + what + ": maximum is 20x framing, not a constant", log);

            // ---- one notch in, one notch out, back where we started ----------------------
            float before = cam.distance;
            float yaw0 = cam.yaw, pitch0 = cam.pitch;
            Vector3 pivot0 = cam.pivot;
            cam.ZoomByNotches(1f);
            Check(cam.TargetDistance < before, "zoom " + what + ": wheel up moves closer", log);
            cam.ZoomByNotches(-1f);
            Check(Mathf.Abs(cam.TargetDistance - before) < before * 1e-4f,
                  "zoom " + what + ": one notch in then out returns exactly (symmetric)", log);

            // ---- angle and target are preserved -------------------------------------------
            cam.ZoomByNotches(5f);
            for (int f = 0; f < 200; f++) cam.AdvanceZoom(1f / 60f);
            Check(cam.yaw == yaw0 && cam.pitch == pitch0,
                  "zoom " + what + ": camera angle unchanged by zoom", log);
            Check(cam.pivot == pivot0, "zoom " + what + ": orbit target unchanged by zoom", log);
            Check(Vector3.Distance(cam.transform.position, cam.pivot) > 0f,
                  "zoom " + what + ": camera is not AT the focus point", log);

            // ---- the minimum holds, and the camera never crosses the pivot ----------------
            for (int n = 0; n < 400; n++) cam.ZoomByNotches(1f);
            for (int f = 0; f < 400; f++) cam.AdvanceZoom(1f / 60f);
            Check(cam.distance >= cam.MinDistance - 1e-6f,
                  "zoom " + what + ": 400 notches in stops at the minimum (" + cam.distance + ")", log);
            Check(cam.distance > 0f, "zoom " + what + ": distance never reaches zero", log);
            float dot = Vector3.Dot(cam.pivot - cam.transform.position, cam.transform.forward);
            Check(dot > 0f, "zoom " + what + ": camera still looks AT the pivot (never flipped)", log);

            // ---- and the maximum ----------------------------------------------------------
            for (int n = 0; n < 800; n++) cam.ZoomByNotches(-1f);
            for (int f = 0; f < 600; f++) cam.AdvanceZoom(1f / 60f);
            Check(cam.distance <= cam.MaxDistance + 1e-4f,
                  "zoom " + what + ": 800 notches out stops at the maximum (" + cam.distance + ")", log);
            dot = Vector3.Dot(cam.pivot - cam.transform.position, cam.transform.forward);
            Check(dot > 0f, "zoom " + what + ": still looks at the pivot when fully out", log);

            UnityEngine.Object.DestroyImmediate(go);
        }

        // ---- smoothing is framerate-independent -----------------------------------------
        {
            GameObject a, b;
            WmvOrbitCamera fast = NewCamera(out a);
            WmvOrbitCamera slow = NewCamera(out b);
            var bounds = new Bounds(Vector3.zero, Vector3.one * 2f);
            fast.Frame(bounds); slow.Frame(bounds);
            fast.ZoomByNotches(6f); slow.ZoomByNotches(6f);
            // Half a second of animation, at 240 fps and at 30 fps.
            for (int i = 0; i < 120; i++) fast.AdvanceZoom(1f / 240f);
            for (int i = 0; i < 15; i++) slow.AdvanceZoom(1f / 30f);
            float diff = Mathf.Abs(fast.distance - slow.distance) / Mathf.Max(fast.distance, 1e-6f);
            Check(diff < 0.02f,
                  "zoom: the same gesture lands within 2% at 240 fps and at 30 fps (" +
                  (diff * 100f).ToString("F2") + "%)", log);
            UnityEngine.Object.DestroyImmediate(a);
            UnityEngine.Object.DestroyImmediate(b);
        }

        // ---- a model switch re-derives everything and leaves nothing gliding -------------
        {
            GameObject go;
            WmvOrbitCamera cam = NewCamera(out go);
            cam.Frame(new Bounds(Vector3.zero, Vector3.one * 2f));
            cam.ZoomByNotches(8f);                       // leave a zoom in flight
            float smallMin = cam.MinDistance;
            cam.Frame(new Bounds(new Vector3(10f, 0f, 0f), Vector3.one * 200f));
            Check(cam.distance == cam.TargetDistance,
                  "zoom: a model switch lands settled, with no zoom still gliding", log);
            Check(cam.distance == cam.FramedDistance,
                  "zoom: a model switch re-frames rather than keeping the old distance", log);
            Check(cam.MinDistance > smallMin,
                  "zoom: the range is re-derived for the new model", log);
            Check(cam.pivot == new Vector3(10f, 0f, 0f),
                  "zoom: a model switch moves the orbit target to the new bounds", log);
            // ... and the wheel still works afterwards, which is the "switching models must not
            // break camera controls" requirement.
            float after = cam.TargetDistance;
            cam.ZoomByNotches(1f);
            Check(cam.TargetDistance < after, "zoom: the wheel still works after a model switch", log);
            UnityEngine.Object.DestroyImmediate(go);
        }

        // ---- a character rebuilt on its other model keeps the view (protocol 8 keepView) --
        {
            GameObject go, refGo;
            WmvOrbitCamera cam = NewCamera(out go);
            cam.Frame(new Bounds(new Vector3(0f, 1f, 0f), new Vector3(1f, 2f, 0.6f)));
            cam.yaw += 40f;                              // an orbit drag
            cam.pitch = 25f;
            cam.pivot += new Vector3(0.1f, 0.3f, 0f);    // a pan
            cam.ZoomByNotches(3f);
            for (int f = 0; f < 200; f++) cam.AdvanceZoom(1f / 60f);
            float yaw0 = cam.yaw, pitch0 = cam.pitch, distance0 = cam.distance;
            Vector3 pivot0 = cam.pivot;
            var other = new Bounds(new Vector3(0f, 1.05f, 0f), new Vector3(1.1f, 2.1f, 0.7f));
            cam.KeepViewFor(other);
            WmvOrbitCamera reference = NewCamera(out refGo);
            reference.Frame(other);
            Check(cam.yaw == yaw0 && cam.pitch == pitch0 && cam.pivot == pivot0,
                  "keep view: the angle and the orbit target stay where the user left them", log);
            Check(cam.distance == distance0 && cam.TargetDistance == distance0,
                  "keep view: the zoom stays, settled", log);
            Check(Mathf.Abs(cam.FramedDistance - reference.FramedDistance) < 1e-5f &&
                  Mathf.Abs(cam.MinDistance - reference.MinDistance) < 1e-5f &&
                  Mathf.Abs(cam.MaxDistance - reference.MaxDistance) < 1e-4f,
                  "keep view: the framing distance and the zoom range are the new model's", log);
            // A zoom outside the new model's range is brought inside it, not kept.
            cam.Frame(new Bounds(Vector3.zero, Vector3.one * 2f));
            for (int n = 0; n < 800; n++) cam.ZoomByNotches(-1f);
            for (int f = 0; f < 600; f++) cam.AdvanceZoom(1f / 60f);
            cam.KeepViewFor(new Bounds(Vector3.zero, Vector3.one * 0.02f));
            Check(cam.distance <= cam.MaxDistance + 1e-4f && cam.TargetDistance <= cam.MaxDistance + 1e-4f,
                  "keep view: a zoom past the new model's range is brought inside it (" + cam.distance + " <= " + cam.MaxDistance + ")", log);
            float kept = cam.TargetDistance;
            cam.ZoomByNotches(1f);
            Check(cam.TargetDistance < kept, "keep view: the wheel still works after it", log);
            UnityEngine.Object.DestroyImmediate(go);
            UnityEngine.Object.DestroyImmediate(refGo);
        }

        // ---- zoom leaves orbit and pan alone ---------------------------------------------
        {
            GameObject go;
            WmvOrbitCamera cam = NewCamera(out go);
            cam.Frame(new Bounds(Vector3.zero, Vector3.one * 2f));
            cam.ZoomByNotches(4f);
            for (int f = 0; f < 200; f++) cam.AdvanceZoom(1f / 60f);
            float zoomed = cam.distance;
            cam.yaw += 90f;                              // what an orbit drag does
            cam.pivot += new Vector3(1f, 0f, 0f);        // what a pan drag does
            cam.AdvanceZoom(1f / 60f);
            Check(Mathf.Abs(cam.distance - zoomed) < 1e-5f,
                  "zoom: orbiting and panning after a zoom does not disturb the distance", log);
            UnityEngine.Object.DestroyImmediate(go);
        }
    }

    static void GeosetTests(Action<string> log)
    {
        // A Drakestalker-shaped component: two submeshes at id 0 and one alternative at 2602.
        byte[] m2 = M2Synthetic.GeosetModel(3);
        byte[] sk = M2Synthetic.GeosetSkin(new[] { 0, 0, 2602 });
        M2ParsedModel model = M2Parser.Parse(m2, 0);
        M2ParsedSkin skin = M2SkinParser.Parse(sk);

        WmvRuntimeModel raw = WmvModelBuilder.Build(model, skin, new Dictionary<int, BlpImage>(),
                                                    "GeosetTestRaw", log, null);
        Check(raw != null, "geoset: component built with no selection reported", log);
        if (raw == null) return;
        Check(DrawnCount(raw) == 2, "geoset: raw component draws the two id-0 submeshes only", log);
        Check(!WmvModelBuilder.GeosetVisibleFor(raw, 2), "geoset: raw component withholds 2602", log);

        // The same component with the state an item would report for it.
        var itemSet = new HashSet<int>(new[] { 2602 });
        WmvRuntimeModel item = WmvModelBuilder.Build(model, skin, new Dictionary<int, BlpImage>(),
                                                     "GeosetTestItem", log, itemSet);
        Check(item != null, "geoset: component built with the item's selection", log);
        if (item == null) { raw.Dispose(); return; }
        Check(DrawnCount(item) == 3, "geoset: the item's selection draws all three submeshes", log);
        Check(WmvModelBuilder.GeosetVisibleFor(item, 2), "geoset: 2602 drawn when the item names it", log);

        // Alternatives in one group: exactly one of them draws, and changing which does not rebuild.
        byte[] am2 = M2Synthetic.GeosetModel(5);
        byte[] askin = M2Synthetic.GeosetSkin(new[] { 0, 2701, 2702, 2703, 2704 });
        M2ParsedModel amodel = M2Parser.Parse(am2, 0);
        M2ParsedSkin askn = M2SkinParser.Parse(askin);
        WmvRuntimeModel alt = WmvModelBuilder.Build(amodel, askn, new Dictionary<int, BlpImage>(),
                                                    "GeosetTestAlt", log, new HashSet<int>(new[] { 2702 }));
        Check(alt != null, "geoset: alternatives model built", log);
        if (alt == null) { raw.Dispose(); item.Dispose(); return; }
        Check(DrawnCount(alt) == 2, "geoset: id 0 plus exactly one alternative draw", log);
        Check(WmvModelBuilder.GeosetVisibleFor(alt, 2), "geoset: the selected alternative 2702 draws", log);
        Check(!WmvModelBuilder.GeosetVisibleFor(alt, 1) && !WmvModelBuilder.GeosetVisibleFor(alt, 3) &&
              !WmvModelBuilder.GeosetVisibleFor(alt, 4), "geoset: its three siblings do not draw", log);

        // Change the selection. Nothing may be recreated.
        var meshBefore = alt.Mesh;
        var matsBefore = alt.Materials;
        var mat0Before = (alt.Materials != null && alt.Materials.Length > 0) ? alt.Materials[0] : null;
        WmvModelBuilder.ApplyGeosets(alt, new HashSet<int>(new[] { 2704 }), log);
        Check(!WmvModelBuilder.GeosetVisibleFor(alt, 2), "geoset: 2702 withheld after the change", log);
        Check(WmvModelBuilder.GeosetVisibleFor(alt, 4), "geoset: 2704 restored after the change", log);
        Check(DrawnCount(alt) == 2, "geoset: still exactly one alternative after the change", log);
        Check(ReferenceEquals(alt.Mesh, meshBefore), "geoset: the mesh was NOT recreated", log);
        Check(ReferenceEquals(alt.Materials, matsBefore), "geoset: the material array was NOT recreated", log);
        Check(alt.Materials != null && alt.Materials.Length > 0 &&
              ReferenceEquals(alt.Materials[0], mat0Before), "geoset: material 0 was NOT recreated", log);

        // An explicitly empty selection means id 0 alone, and agrees with no selection at all.
        WmvModelBuilder.ApplyGeosets(alt, new HashSet<int>(), log);
        Check(DrawnCount(alt) == 1, "geoset: an empty selection draws id 0 alone", log);
        WmvModelBuilder.ApplyGeosets(alt, null, log);
        Check(DrawnCount(alt) == 1, "geoset: no selection draws id 0 alone, as an empty one does", log);

        // The diagnostic override must be inert unless it was passed.
        Check(WmvModelBuilder.Debug_.OnlySubmeshes == null,
              "geoset: -wmvOnlySubmesh is unset, so the geoset rule alone decides", log);

        raw.Dispose(); item.Dispose(); alt.Dispose();
    }

    static string Bits(bool[] v)
    {
        var sb = new System.Text.StringBuilder();
        foreach (bool b in v) sb.Append(b ? '1' : '0');
        return sb.ToString();
    }

    /// <summary>What the mesh itself holds for each built entry right now: 1 = its triangles, 0 = the
    /// empty list. Read back from the mesh, so the incremental SetTriangles bookkeeping is what is
    /// tested, not the predicate alone.</summary>
    static string MeshBits(WmvRuntimeModel rt)
    {
        var sb = new System.Text.StringBuilder();
        for (int i = 0; i < rt.Mesh.subMeshCount; i++)
            sb.Append(rt.Mesh.GetIndexCount(i) > 0 ? '1' : '0');
        return sb.ToString();
    }

    /// <summary>Triangles the built entries of these on-skin-submeshes contribute, gate ignored.</summary>
    static int TrianglesOf(WmvRuntimeModel rt, bool[] on)
    {
        int n = 0;
        for (int i = 0; i < rt.SubmeshIndices.Length; i++)
            if (on[rt.SubmeshIndices[i]]) n += rt.SubmeshTriangles[i].Length / 3;
        return n;
    }

    /// <summary>
    /// THE HOST'S PER-SUBMESH STATE -- the Geosets checkboxes -- applied live.
    ///
    /// The geoset-id rule cannot say "hide this id-0 submesh" or "hide one of the two submeshes that
    /// share id 2701"; the host's own display flags can, and they are what the host's (archived) OpenGL
    /// renderer drew from. So: the explicit state decides when it is present, the id rule still decides when it is
    /// not, a switch is incremental (nothing is recreated), a list that does not fit the skin is
    /// refused without touching anything, the last of a sequence of switches is what shows, and a
    /// material gate that is closed right now still withholds its batch.
    /// </summary>
    static void SubmeshVisibilityTests(Action<string> log)
    {
        // id 0 twice, 2701 twice (two submeshes share one id), 2702 once.
        byte[] m2 = M2Synthetic.GeosetModel(5);
        byte[] sk = M2Synthetic.GeosetSkin(new[] { 0, 0, 2701, 2701, 2702 });
        M2ParsedModel model = M2Parser.Parse(m2, 0);
        M2ParsedSkin skin = M2SkinParser.Parse(sk);
        var ids = new HashSet<int>(new[] { 2701 });

        // No explicit state: the id rule, exactly as before.
        WmvRuntimeModel rt = WmvModelBuilder.Build(model, skin, new Dictionary<int, BlpImage>(),
                                                   "SubmeshVisTest", log, ids, null);
        Check(rt != null, "submesh vis: model built", log);
        if (rt == null) return;
        Check(rt.SkinSubmeshCount == 5 && rt.SubmeshVisible == null,
              "submesh vis: no explicit state at build, skin submesh count kept (5)", log);
        Check(Bits(WmvModelBuilder.EffectiveSubmeshVisibility(rt)) == "11110",
              "submesh vis: without it the id rule decides (id 0 + both 2701, not 2702) = " +
              Bits(WmvModelBuilder.EffectiveSubmeshVisibility(rt)), log);

        var meshBefore = rt.Mesh;
        var matsBefore = rt.Materials;
        var mat0Before = rt.Materials.Length > 0 ? rt.Materials[0] : null;
        var triBefore = rt.SubmeshTriangles;

        // Hide an id-0 submesh -- impossible under the id rule.
        bool[] a = { false, true, true, true, false };
        int tri = WmvModelBuilder.ApplySubmeshVisibility(rt, a, log);
        Check(Bits(WmvModelBuilder.EffectiveSubmeshVisibility(rt)) == "01110",
              "submesh vis: an id-0 submesh can be switched off", log);
        Check(tri == TrianglesOf(rt, a) && rt.TriangleCount == tri,
              "submesh vis: triangles drawn = the switched-on entries' (" + tri + ")", log);
        Check(MeshBits(rt) == "01110", "submesh vis: the MESH holds exactly those entries (" + MeshBits(rt) + ")", log);
        Check(!WmvModelBuilder.GeosetVisibleFor(rt, 0) && WmvModelBuilder.GeosetVisibleFor(rt, 1),
              "submesh vis: the material animator's predicate agrees (entry 0 off, entry 1 on)", log);

        // Hide ONE of the two submeshes that share id 2701.
        bool[] b = { false, true, true, false, false };
        tri = WmvModelBuilder.ApplySubmeshVisibility(rt, b, log);
        Check(Bits(WmvModelBuilder.EffectiveSubmeshVisibility(rt)) == "01100",
              "submesh vis: one of two submeshes sharing an id switched off alone", log);
        Check(tri == TrianglesOf(rt, b), "submesh vis: triangle count follows", log);
        Check(MeshBits(rt) == "01100", "submesh vis: the mesh follows (" + MeshBits(rt) + ")", log);

        // A off, B off, A on: the last state is what shows.
        bool[] s1 = { true, true, false, true, true };   // A (2) off
        bool[] s2 = { true, false, false, true, true };  // B (1) off
        bool[] s3 = { true, false, true, true, true };   // A on again
        WmvModelBuilder.ApplySubmeshVisibility(rt, s1, log);
        WmvModelBuilder.ApplySubmeshVisibility(rt, s2, log);
        tri = WmvModelBuilder.ApplySubmeshVisibility(rt, s3, log);
        Check(Bits(WmvModelBuilder.EffectiveSubmeshVisibility(rt)) == "10111",
              "submesh vis: A off, B off, A on ends with A on and B off", log);
        Check(tri == TrianglesOf(rt, s3), "submesh vis: and draws exactly those triangles", log);
        Check(MeshBits(rt) == "10111", "submesh vis: the mesh holds A and not B (" + MeshBits(rt) + ")", log);

        // Nothing was recreated by any of it.
        Check(ReferenceEquals(rt.Mesh, meshBefore), "submesh vis: the mesh was NOT recreated", log);
        Check(ReferenceEquals(rt.Materials, matsBefore) &&
              (mat0Before == null || ReferenceEquals(rt.Materials[0], mat0Before)),
              "submesh vis: the materials were NOT recreated", log);
        Check(ReferenceEquals(rt.SubmeshTriangles, triBefore), "submesh vis: the kept triangle arrays were reused", log);

        // A list that does not fit the skin is refused, and nothing changes.
        int refused = WmvModelBuilder.ApplySubmeshVisibility(rt, new[] { true, true }, log);
        Check(refused == -1 && Bits(WmvModelBuilder.EffectiveSubmeshVisibility(rt)) == "10111" &&
              MeshBits(rt) == "10111",
              "submesh vis: a list of the wrong length is refused and the state and mesh are kept", log);

        // A closed material gate still withholds its batch, whatever the switch says. The gate is
        // closed the way the material animator closes one: the flag, and its triangles withheld.
        rt.GateHidden[2] = true;
        WmvModelBuilder.ApplyGeosets(rt, rt.Geosets, log);
        Check(MeshBits(rt) == "10011", "submesh vis: closing entry 2's gate withholds it (" + MeshBits(rt) + ")", log);
        bool[] allOn = { true, true, true, true, true };
        tri = WmvModelBuilder.ApplySubmeshVisibility(rt, allOn, log);
        bool[] notEntry2 = { true, true, false, true, true };
        Check(tri == TrianglesOf(rt, notEntry2) && tri == WmvModelBuilder.DrawnTriangleCount(rt),
              "submesh vis: a batch whose gate is closed stays withheld when switched on", log);
        Check(MeshBits(rt) == "11011", "submesh vis: ...and the mesh does not hold it (" + MeshBits(rt) + ")", log);
        Check(WmvModelBuilder.GeosetVisibleFor(rt, 2),
              "submesh vis: ...while the selection still says it is on, so the gate re-opens it later", log);
        rt.GateHidden[2] = false;
        // What the material animator does when a gate opens: re-apply the entries (full pass).
        WmvModelBuilder.ApplyGeosets(rt, rt.Geosets, log);
        Check(MeshBits(rt) == "11111", "submesh vis: the opened gate draws the switched-on entry (" + MeshBits(rt) + ")", log);

        // null hands the decision back to the id rule.
        WmvModelBuilder.ApplySubmeshVisibility(rt, null, log);
        Check(rt.SubmeshVisible == null && Bits(WmvModelBuilder.EffectiveSubmeshVisibility(rt)) == "11110",
              "submesh vis: clearing the explicit state restores the id rule", log);

        // A skin push's id change does not override an explicit state that is present.
        WmvModelBuilder.ApplySubmeshVisibility(rt, b, log);
        WmvModelBuilder.ApplyGeosets(rt, new HashSet<int>(new[] { 2702 }), log);
        Check(Bits(WmvModelBuilder.EffectiveSubmeshVisibility(rt)) == "01100",
              "submesh vis: a geoset-id change leaves the explicit state deciding", log);

        // Built WITH an explicit state: honoured from the first frame; a wrong-length one is ignored.
        WmvRuntimeModel built = WmvModelBuilder.Build(model, skin, new Dictionary<int, BlpImage>(),
                                                      "SubmeshVisBuilt", log, ids, b);
        Check(built != null && built.SubmeshVisible != null &&
              Bits(WmvModelBuilder.EffectiveSubmeshVisibility(built)) == "01100" &&
              built.TriangleCount == TrianglesOf(built, b),
              "submesh vis: a model built with the host's state draws it from the start", log);
        WmvRuntimeModel wrong = WmvModelBuilder.Build(model, skin, new Dictionary<int, BlpImage>(),
                                                      "SubmeshVisWrong", log, ids, new[] { true });
        Check(wrong != null && wrong.SubmeshVisible == null &&
              Bits(WmvModelBuilder.EffectiveSubmeshVisibility(wrong)) == "11110",
              "submesh vis: a build with a list that does not fit the skin falls back to the id rule", log);

        rt.Dispose();
        if (built != null) built.Dispose();
        if (wrong != null) wrong.Dispose();
    }

    /// <summary>
    /// THE OUTPUT GATE, tested against real materials rather than against a copy of the arithmetic.
    ///
    /// WmvOpaque.shader skips the preview-light roll-off exactly where the preview light did not
    /// apply, and the thing that decides that is _Emissive: the shader's bypass reads it, and the
    /// builder writes it from the M2 blend mode (additive) or the material's own 0x01 UNLIT flag.
    /// A fragment program cannot be run from either harness, so the curve itself is pinned as
    /// arithmetic in the parser suite and said there to be a mirror. What CAN be tested here, in
    /// the player, on materials the real builder produced, is the gate's INPUT -- which is the part
    /// this branch actually changed. If _Emissive ever stopped tracking blend mode and the unlit
    /// flag, the exemption would silently apply to the wrong passes and nothing else would notice.
    /// </summary>
    static void OutputGateTests(Action<string> log)
    {
        // EVERY M2 blend mode, so the boundary is pinned from both sides rather than sampled,
        // plus the two cases where the material's own 0x01 UNLIT flag is what decides.
        int[] flags  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01 };
        int[] blends = {    0,    1,    2,    3,    4,    5,    6,    7,    0,    4 };
        bool[] wantEmissive =
        {
            false, false, false, true, true, false, false, false, true, true
        };
        string[] what =
        {
            "blend 0 opaque, not unlit -> lit, the curve applies",
            "blend 1 alpha key -> a cut-out surface is still lit",
            "blend 2 alpha blend -> a lit surface seen through transparency",
            "blend 3 NoAlphaAdd (One/One) -> emissive, the curve is skipped",
            "blend 4 Add (SrcAlpha/One) -> emissive, the curve is skipped",
            "blend 5 modulate -> multiplies the destination, still lit",
            "blend 6 modulate 2x -> still lit",
            "blend 7 BlendAdd (One/OneMinusSrcAlpha) -> premultiplied, NOT in the exempt set",
            "the 0x01 UNLIT flag makes an OPAQUE pass emissive on its own",
            "additive AND unlit -> emissive, the two sources agree rather than fight",
        };

        byte[] m2 = M2Synthetic.MaterialModeModel(flags, blends);
        byte[] sk = M2Synthetic.MaterialModeSkin(flags.Length);
        M2ParsedModel model = M2Parser.Parse(m2, 0);
        M2ParsedSkin skin = M2SkinParser.Parse(sk);
        WmvRuntimeModel rt = WmvModelBuilder.Build(model, skin, new Dictionary<int, BlpImage>(),
                                                   "OutputGateTest", log, null);
        Check(rt != null, "output gate: the material-mode model built", log);
        if (rt == null) return;
        Check(rt.Materials != null && rt.Materials.Length == flags.Length,
              "output gate: one material per case reached the runtime", log);
        if (rt.Materials == null || rt.Materials.Length != flags.Length) { rt.Dispose(); return; }

        for (int i = 0; i < flags.Length; i++)
        {
            Material m = rt.Materials[i];
            bool has = m != null && m.HasProperty("_Emissive");
            Check(has, "output gate: material " + i + " carries _Emissive at all", log);
            if (!has) continue;
            bool got = m.GetFloat("_Emissive") > 0.5f;
            Check(got == wantEmissive[i], "output gate: " + what[i], log);
        }

        // The exemption is gated on the SHIPPED rig as well; if the legacy rig were live the
        // curve would be skipped on the A/B baseline too and that baseline would stop meaning
        // anything. -wmvRig defaults to 0.
        Check(WmvModelBuilder.Debug_.Rig < 0.5f,
              "output gate: the shipped rig is the default, so the exemption is the live path", log);

        rt.Dispose();
    }

    // ---------------------------------------------------------------- characters

    static bool NearV(Vector3 a, Vector3 b, float eps)
    {
        return Mathf.Abs(a.x - b.x) < eps && Mathf.Abs(a.y - b.y) < eps && Mathf.Abs(a.z - b.z) < eps;
    }

    /// <summary>One rotation, whichever of its two signs each quaternion carries.</summary>
    static bool SameRotation(Quaternion a, Quaternion b)
    {
        float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        return Mathf.Abs(dot) > 1f - 1e-5f;
    }

    static bool SameMatrix(Matrix4x4 a, Matrix4x4 b)
    {
        for (int i = 0; i < 16; i++)
            if (Mathf.Abs(a[i] - b[i]) > 1e-5f)
                return false;
        return true;
    }

    static bool PoseIs(Transform t, Vector3 position, Quaternion rotation, Vector3 scale)
    {
        return t != null && NearV(t.localPosition, position, 1e-4f) && SameRotation(t.localRotation, rotation) &&
               NearV(t.localScale, scale, 1e-4f);
    }

    static Vector3 UnityPosition(WowVec3 v)
    {
        float x, y, z;
        WowCoordinateConverter.ConvertPosition(v, out x, out y, out z);
        return new Vector3(x, y, z);
    }

    static Vector3 UnityScale(WowVec3 v)
    {
        float x, y, z;
        WowCoordinateConverter.ConvertScale(v, out x, out y, out z);
        return new Vector3(x, y, z);
    }

    static Quaternion UnityRotation(WowQuat q)
    {
        float x, y, z, w;
        WowCoordinateConverter.ConvertRotation(q, out x, out y, out z, out w);
        return new Quaternion(x, y, z, w);
    }

    /// <summary>Total weight a vertex gives one bone, over its four slots in whatever order the mesh
    /// keeps them.</summary>
    static float WeightOn(BoneWeight w, int bone)
    {
        float s = 0f;
        if (w.boneIndex0 == bone) s += w.weight0;
        if (w.boneIndex1 == bone) s += w.weight1;
        if (w.boneIndex2 == bone) s += w.weight2;
        if (w.boneIndex3 == bone) s += w.weight3;
        return s;
    }

    /// <summary>Two influences on one parsed vertex, the other two slots cleared.</summary>
    static void Influence(M2ParsedModel model, int vertex, byte bone0, byte weight0, byte bone1, byte weight1)
    {
        model.Vertices[vertex].BoneIndex0 = bone0; model.Vertices[vertex].BoneWeight0 = weight0;
        model.Vertices[vertex].BoneIndex1 = bone1; model.Vertices[vertex].BoneWeight1 = weight1;
        model.Vertices[vertex].BoneIndex2 = 0; model.Vertices[vertex].BoneWeight2 = 0;
        model.Vertices[vertex].BoneIndex3 = 0; model.Vertices[vertex].BoneWeight3 = 0;
    }

    /// <summary>A 2x2 texture of one opaque colour, so which image a material ended up with can be told
    /// from a single texel.</summary>
    static BlpImage SolidTexture(byte r, byte g, byte b)
    {
        var img = new BlpImage { Width = 2, Height = 2, Rgba = new byte[2 * 2 * 4], Encoding = "test" };
        for (int i = 0; i < img.Rgba.Length; i += 4)
        {
            img.Rgba[i] = r; img.Rgba[i + 1] = g; img.Rgba[i + 2] = b; img.Rgba[i + 3] = 255;
        }
        return img;
    }

    /// <summary>Which colour a material's base texture holds: 'r', 'g', 'b', or '?' when there is no
    /// readable base texture at all.</summary>
    static char BaseColour(Material m)
    {
        var tex = m != null ? m.mainTexture as Texture2D : null;
        if (tex == null)
            return '?';
        Color c = tex.GetPixel(0, 0);
        if (c.r > 0.5f && c.g < 0.5f && c.b < 0.5f) return 'r';
        if (c.g > 0.5f && c.r < 0.5f && c.b < 0.5f) return 'g';
        if (c.b > 0.5f && c.r < 0.5f && c.g < 0.5f) return 'b';
        return '?';
    }

    static M2Track<WowVec3> VectorKey(float x, float y, float z)
    {
        return new M2Track<WowVec3>
        {
            Interpolation = M2Interpolation.Linear, GlobalSequence = -1,
            Times = new uint[] { 0 }, Values = new[] { new WowVec3(x, y, z) },
        };
    }

    static M2Track<WowQuat> RotationKey(float x, float y, float z, float w)
    {
        return new M2Track<WowQuat>
        {
            Interpolation = M2Interpolation.Linear, GlobalSequence = -1,
            Times = new uint[] { 0 }, Values = new[] { new WowQuat(x, y, z, w) },
        };
    }

    /// <summary>
    /// A PLAYABLE CHARACTER is several builds that lean on each other: a body, merged parts skinned to
    /// the body's bones, attached items hanging off them, and a closed hand posed from another
    /// sequence. Each of those joints is checked here on synthetic models built by the real builder;
    /// what a character wears is the host's business and is not.
    /// </summary>
    static void CharacterTests(Action<string> log)
    {
        ExternalSkeletonTests(log);
        BaseTextureOverrideTests(log);
        PoseOverrideTests(log);
        GlobalClockTests(log);
        AttachmentPlacementTests(log);
    }

    /// <summary>
    /// A MERGED PART, skinned to another model's skeleton (WmvBuildOptions.Skeleton).
    ///
    /// The part has no bones of its own: renderer bone i is the body's bone BoneMap[i], bound with that
    /// bone's bind pose, and an influence on a bone the map cannot place is dropped. Every way that goes
    /// wrong is quiet on screen -- an off-by-one in the map puts a sleeve on the other arm, a bind pose
    /// taken by the part's own index drags it across the body, an influence left on the stand-in bone
    /// pulls vertices toward it -- so each is read back from the renderer and mesh the builder made,
    /// and the skinning itself from a CPU bake.
    /// </summary>
    static void ExternalSkeletonTests(Action<string> log)
    {
        const int skelId = 777001, skinId = 777003;
        byte[] bodyM2 = M2Synthetic.SkeletonModel(skelId, skinId);
        M2ParsedSkin bodySkin = M2SkinParser.Parse(M2Synthetic.TransformSwitchSkin());

        // The body's bones live in a .skel. Unapplied, that model cannot be skinned; applied, it must be,
        // or a character body would never have bones to merge anything onto.
        M2ParsedModel unapplied = M2Parser.Parse(bodyM2);
        WmvRuntimeModel still = WmvModelBuilder.Build(unapplied, bodySkin, new Dictionary<int, BlpImage>(),
                                                      "SkidUnapplied", s => log("  " + s));
        Check(still != null && !still.Skinned, "character: an SKID model whose skeleton was not applied is drawn static", log);
        if (still != null) still.Dispose();

        M2ParsedModel bodyModel = M2Parser.Parse(bodyM2);
        M2Parser.ApplySkeleton(bodyModel, M2Synthetic.Skeleton(0), null, -1);
        WmvRuntimeModel body = WmvModelBuilder.Build(bodyModel, bodySkin, new Dictionary<int, BlpImage>(),
                                                     "ExternalSkeletonBody", s => log("  " + s));
        bool bodyOk = body != null && body.Skinned && body.Bones.Length == 3 && body.BindPoses.Length == 3;
        Check(bodyOk, "character: an SKID model whose skeleton WAS applied is skinned, with a bind pose per bone", log);
        if (!bodyOk) { if (body != null) body.Dispose(); return; }
        Check(!SameMatrix(body.BindPoses[2], body.BindPoses[0]),
              "character: the body's bind poses differ per bone, so a wrong mapping would show", log);

        // The part: two triangles and no bones. Its influences are written here rather than in a fixture
        // because they are the test. Part bone 0 -> body bone 2 (the leaf), part bone 1 -> body bone 0
        // (the root), part bone 2 -> 99, which the body does not have.
        M2ParsedModel partModel = M2Parser.Parse(M2Synthetic.GeosetModel(2), 0);
        M2ParsedSkin partSkin = M2SkinParser.Parse(M2Synthetic.GeosetSkin(new[] { 0, 0 }));
        for (int i = 0; i < 3; i++)
            Influence(partModel, i, 0, 255, 0, 0);        // first triangle: all on part bone 0
        Influence(partModel, 3, 1, 255, 0, 0);            // on part bone 1
        Influence(partModel, 4, 1, 128, 2, 127);          // split between a placeable and an unplaceable bone
        Influence(partModel, 5, 2, 255, 0, 0);            // only on the unplaceable bone

        var options = new WmvBuildOptions
        {
            NoAnimation = true,
            Skeleton = new WmvExternalSkeleton
            {
                Bones = body.Bones, BindPoses = body.BindPoses, BoneMap = new[] { 2, 0, 99 }, LocalBounds = body.Bounds,
            },
        };
        WmvRuntimeModel part = WmvModelBuilder.Build(partModel, partSkin, new Dictionary<int, BlpImage>(),
                                                     "ExternalSkeletonPart", s => log("  " + s), null, null, options);
        Check(part != null && part.Skin != null && part.Skin.sharedMesh == part.Mesh,
              "character: the part is built as a skinned renderer over its own mesh", log);
        if (part == null || part.Skin == null) { if (part != null) part.Dispose(); body.Dispose(); return; }
        part.Root.transform.SetParent(body.Root.transform, false);     // where the dresser hangs it

        Transform[] bones = part.Skin.bones;
        Check(bones.Length == 3 && bones[0] == body.Bones[2] && bones[1] == body.Bones[0],
              "character: renderer bone i is the body bone the map names (0 -> body 2, 1 -> body 0)", log);
        Check(bones.Length == 3 && bones[2] != null,
              "character: the entry the map cannot place still holds a transform (a null bone stops the renderer)", log);
        Matrix4x4[] poses = part.Mesh.bindposes;
        Check(poses.Length == 3 && SameMatrix(poses[0], body.BindPoses[2]) && SameMatrix(poses[1], body.BindPoses[0]),
              "character: bind pose i is the mapped body bone's bind pose, not the one at the part's own index", log);

        BoneWeight[] w = part.Mesh.boneWeights;
        bool sixWeights = w.Length == 6;
        bool noneOnUnplaceable = sixWeights;
        for (int i = 0; noneOnUnplaceable && i < w.Length; i++)
            noneOnUnplaceable = WeightOn(w[i], 2) == 0f;
        Check(noneOnUnplaceable, "character: no vertex keeps any weight on the bone the map cannot place", log);
        Check(sixWeights && Near(WeightOn(w[0], 0), 1f) && Near(WeightOn(w[3], 1), 1f),
              "character: influences on placeable bones are kept as they were", log);
        Check(sixWeights && Near(WeightOn(w[4], 1), 1f),
              "character: a vertex split with an unplaceable bone is renormalised onto the placeable one", log);
        Check(sixWeights && Near(WeightOn(w[5], 0), 1f),
              "character: a vertex weighted only to an unplaceable bone falls back to bone 0 at full weight", log);

        Check(!part.Skinned && part.Bones.Length == 0 && part.BoneRestPositions.Length == 0 && part.BindPoses.Length == 0,
              "character: the part owns no bones, so a sequence change never poses the body's through it", log);
        Check(part.Animator == null && part.Emitters == null && part.Root.GetComponent<WmvM2Animator>() == null,
              "character: NoAnimation -- the part has no animator and no emitters", log);

        // The skinning, baked on the CPU. With the body at rest its bind poses cancel and the part lands
        // where its file put it; move one body bone and exactly the vertices mapped to it follow, by
        // exactly that much.
        if (body.Animator != null)
            body.Animator.RestorePose();
        Vector3[] file = part.Mesh.vertices;
        var baked = new Mesh();
        part.Skin.BakeMesh(baked, true);
        Vector3[] rest = baked.vertices;
        bool atRest = rest.Length == file.Length && rest.Length == 6;
        for (int i = 0; atRest && i < rest.Length; i++)
            atRest = NearV(rest[i], file[i], 1e-3f);
        Check(atRest, "character: with the body at rest the part bakes to its own vertex positions", log);

        var delta = new Vector3(0f, 0.75f, 0f);
        Transform leaf = body.Bones[2];
        leaf.position = leaf.position + delta;
        part.Skin.BakeMesh(baked, true);
        Vector3[] moved = baked.vertices;
        bool follows = moved.Length == 6 && rest.Length == 6, stays = follows;
        for (int i = 0; follows && i < 3; i++)
            follows = NearV(moved[i], rest[i] + delta, 1e-3f);
        for (int i = 3; stays && i < 5; i++)
            stays = NearV(moved[i], rest[i], 1e-3f);
        Check(follows, "character: moving body bone 2 moves the part's vertices mapped to it by the same amount", log);
        Check(stays, "character: ... and leaves the vertices mapped to body bone 0 where they were", log);

        UnityEngine.Object.Destroy(baked);
        part.Dispose();
        body.Dispose();
    }

    /// <summary>
    /// THE HAND-TEXTURE RULE (WmvBuildOptions.BaseTextureOverride). A merged part's hand submeshes bind
    /// an image the host names instead of their own slot. The override is keyed by SKIN submesh, and
    /// the binding has to remember the key or the next texture rebind would put the part's own slot
    /// back on the hands.
    /// </summary>
    static void BaseTextureOverrideTests(Action<string> log)
    {
        const int overrideKey = 100000;      // far outside any M2 texture table, as the dresser's hand key is
        M2ParsedModel model = M2Parser.Parse(M2Synthetic.GeosetModel(2), 0);
        M2ParsedSkin skin = M2SkinParser.Parse(M2Synthetic.GeosetSkin(new[] { 0, 0 }));
        var textures = new Dictionary<int, BlpImage>
        {
            { 0, SolidTexture(0, 255, 0) },                  // slot 0, which both batches name
            { overrideKey, SolidTexture(255, 0, 0) },
        };
        var options = new WmvBuildOptions { BaseTextureOverride = new Dictionary<int, int> { { 1, overrideKey } } };
        WmvRuntimeModel rt = WmvModelBuilder.Build(model, skin, textures, "BaseTextureOverrideTest",
                                                   s => log("  " + s), null, null, options);
        Check(rt != null && rt.SubmeshIndices.Length == 2 && rt.Materials.Length == 2 && rt.Bindings.Length == 2,
              "texture override: two-submesh model built", log);
        if (rt == null || rt.SubmeshIndices.Length != 2 || rt.Materials.Length != 2 || rt.Bindings.Length != 2)
        {
            if (rt != null) rt.Dispose();
            return;
        }
        int own = rt.SubmeshIndices[0] == 0 ? 0 : 1;
        int over = 1 - own;

        Check(BaseColour(rt.Materials[own]) == 'g',
              "texture override: a submesh the override does not name binds its own slot", log);
        Check(BaseColour(rt.Materials[over]) == 'r',
              "texture override: the named submesh binds the override key's image on unit 0", log);
        Check(rt.Materials[own].mainTexture != rt.Materials[over].mainTexture,
              "texture override: ... which is a different texture from the one its own slot gives", log);
        Check(rt.Bindings[over].BaseSlot == overrideKey && rt.Bindings[own].BaseSlot == 0,
              "texture override: the binding records the key, not the slot", log);

        // What the dresser does when the hand image changes: the same keys, a new image behind one.
        textures[overrideKey] = SolidTexture(0, 0, 255);
        WmvModelBuilder.RebindTextures(rt, textures, "BaseTextureOverrideTest", null);
        Check(BaseColour(rt.Materials[over]) == 'b' && BaseColour(rt.Materials[own]) == 'g',
              "texture override: a rebind keeps the override -- the new hand image lands on the named submesh only", log);
        rt.Dispose();
    }

    /// <summary>
    /// THE CLOSED HAND (WmvM2Animator.SetPoseOverride). Some bones take a fixed pose from another
    /// sequence over whatever the playing one does, on every frame, and give it back when released.
    ///
    /// Three bones: 0 and 1 are moved by the playing sequence, 2 is made still in it here. Bones 1 and
    /// 2 are overridden. Bone 1 shows the override REPLACING a sequence pose; bone 2 shows that clearing
    /// really resets, because nothing in the sequence would put it back otherwise; bone 0 shows the rest
    /// of the skeleton keeps playing throughout.
    /// </summary>
    static void PoseOverrideTests(Action<string> log)
    {
        M2ParsedModel model = M2Parser.Parse(M2Synthetic.InFileSkeletonModel(473370), 0);
        model.Bones[2].Translation = new M2Track<WowVec3> { GlobalSequence = -1 };
        model.Bones[2].Rotation = new M2Track<WowQuat> { GlobalSequence = -1 };
        model.Bones[2].Scale = new M2Track<WowVec3> { GlobalSequence = -1 };
        WmvRuntimeModel rt = WmvModelBuilder.Build(model, M2SkinParser.Parse(M2Synthetic.TransformSwitchSkin()),
                                                   new Dictionary<int, BlpImage>(), "PoseOverrideTest",
                                                   s => log("  " + s));
        bool built = rt != null && rt.Skinned && rt.Animator != null && rt.Bones.Length == 3;
        Check(built, "pose override: skinned three-bone model with an animator built", log);
        if (!built) { if (rt != null) rt.Dispose(); return; }
        WmvM2Animator anim = rt.Animator;
        Check(anim.AnimatedBoneCount == 2, "pose override: the sequence moves bones 0 and 1, not bone 2", log);

        // Bone 1's rotation runs on a global sequence. Hold that clock where the track has turned it 90
        // degrees, so the override's identity rotation differs from what the sequence writes.
        double globalWas = WmvM2Animator.GlobalTimeMs;
        WmvM2Animator.GlobalTimeMs = 250.0;

        float[] times = { 0f, 250f, 500f, 750f };
        var pos = new Vector3[times.Length, 3];
        var rot = new Quaternion[times.Length, 3];
        var scl = new Vector3[times.Length, 3];
        for (int k = 0; k < times.Length; k++)
        {
            anim.ApplyPose(times[k]);
            for (int b = 0; b < 3; b++)
            {
                pos[k, b] = rt.Bones[b].localPosition;
                rot[k, b] = rt.Bones[b].localRotation;
                scl[k, b] = rt.Bones[b].localScale;
            }
        }
        Check(!NearV(pos[1, 0], pos[0, 0], 1e-3f),
              "pose override: the sequence moves bone 0 between 0 and 250 ms, so following it is visible", log);
        Check(!SameRotation(rot[0, 1], Quaternion.identity),
              "pose override: the global track turns bone 1, so an identity override is visible", log);

        var tracks = new M2BoneDef[3];
        tracks[1].Translation = VectorKey(0f, 0f, 2f);
        tracks[1].Scale = VectorKey(1f, 2f, 3f);
        tracks[2].Translation = VectorKey(0f, 1f, 0f);
        tracks[2].Rotation = RotationKey(0f, 0f, 0.70710677f, 0.70710677f);
        // Channels with no keys come out at rest: bone 1 unrotated, bone 2 at unit scale.
        Vector3 pos1 = rt.BoneRestPositions[1] + UnityPosition(new WowVec3(0f, 0f, 2f));
        Vector3 scl1 = UnityScale(new WowVec3(1f, 2f, 3f));
        Vector3 pos2 = rt.BoneRestPositions[2] + UnityPosition(new WowVec3(0f, 1f, 0f));
        Quaternion rot2 = UnityRotation(new WowQuat(0f, 0f, 0.70710677f, 0.70710677f));

        // Put the animator's own clock at 250 ms (paused, so it holds there). Setting and clearing the
        // override re-pose the skeleton at THAT time, not at the override's instant: the parameter once
        // hid the clock field and snapped the whole body to the fist's 1 ms.
        anim.StartFromApp(false, 250f, 1f);
        Check(PoseIs(rt.Bones[0], pos[1, 0], rot[1, 0], scl[1, 0]),
              "pose override: the animator's clock is at 250 ms before the override is set", log);

        Check(anim.PoseOverrideCount == 0, "pose override: none before one is set", log);
        anim.SetPoseOverride(rt.Bones, rt.BoneRestPositions, new[] { 1, 2 }, tracks, 1f);
        Check(anim.PoseOverrideCount == 2, "pose override: PoseOverrideCount = 2 after overriding two bones", log);
        Check(PoseIs(rt.Bones[1], pos1, Quaternion.identity, scl1) && PoseIs(rt.Bones[2], pos2, rot2, Vector3.one),
              "pose override: setting it poses the bones at once, without waiting for a frame", log);
        Check(PoseIs(rt.Bones[0], pos[1, 0], rot[1, 0], scl[1, 0]),
              "pose override: ... and the rest of the skeleton stays on the animator's clock (250 ms), not the override's 1 ms", log);

        bool held1 = true, held2 = true, follows0 = true;
        for (int k = 0; k < times.Length; k++)
        {
            anim.ApplyPose(times[k]);
            held1 &= PoseIs(rt.Bones[1], pos1, Quaternion.identity, scl1);
            held2 &= PoseIs(rt.Bones[2], pos2, rot2, Vector3.one);
            follows0 &= PoseIs(rt.Bones[0], pos[k, 0], rot[k, 0], scl[k, 0]);
        }
        Check(held1, "pose override: a bone the sequence moves holds the override at 0/250/500/750 ms", log);
        Check(held2, "pose override: a bone the sequence leaves alone holds the override at every instant too", log);
        Check(follows0, "pose override: the bone not overridden follows the sequence meanwhile", log);

        // The loop above left the bones at 750 ms; clearing re-poses at the clock's 250 ms, not at the 0 passed.
        anim.SetPoseOverride(rt.Bones, rt.BoneRestPositions, new int[0], tracks, 0f);
        Check(anim.PoseOverrideCount == 0, "pose override: an empty index list removes it", log);
        Check(PoseIs(rt.Bones[2], rt.BoneRestPositions[2], Quaternion.identity, Vector3.one),
              "pose override: clearing returns a released bone to rest (rest position, identity, unit scale)", log);
        Check(PoseIs(rt.Bones[1], pos[1, 1], rot[1, 1], scl[1, 1]) && PoseIs(rt.Bones[0], pos[1, 0], rot[1, 0], scl[1, 0]),
              "pose override: ... and a released bone the sequence moves is back on the sequence at once, at the animator's clock (250 ms)", log);
        anim.ApplyPose(500f);
        Check(PoseIs(rt.Bones[1], pos[2, 1], rot[2, 1], scl[2, 1]) && PoseIs(rt.Bones[0], pos[2, 0], rot[2, 0], scl[2, 0]) &&
              PoseIs(rt.Bones[2], rt.BoneRestPositions[2], Quaternion.identity, Vector3.one),
              "pose override: the next ApplyPose follows the sequence again", log);

        WmvM2Animator.GlobalTimeMs = globalWas;
        rt.Dispose();
    }

    /// <summary>
    /// THE GLOBAL CLOCK MOVES ONCE PER FRAME, however many animators run it. A character is a body plus
    /// an animator per attached item, and when each advanced the shared clock the global sequences ran
    /// that many times too fast.
    ///
    /// Time.frameCount cannot advance inside this synchronous test, which is exactly what makes "the
    /// same frame" possible to arrange: LateUpdate (a private Unity message) is invoked through
    /// reflection on two animators back to back. Three private fields are set the same way so the result
    /// does not hang on timing: the global clock's last wall-clock reading and each animator's own are
    /// put in the past, so each call sees a real elapsed time, and the frame stamp is put on the previous
    /// frame so the first call is the frame's first. "The next frame" is the stamp moved back once more.
    ///
    /// The first animator's own reading is put back to the player's start, at least two seconds ago -- an
    /// attached item hidden and shown again -- while the global clock's is 400 ms back: the global clock must take the frame's step
    /// from its own reading, not jump by the minute that one animator was away.
    /// </summary>
    static void GlobalClockTests(Action<string> log)
    {
        if (WmvModelBuilder.Debug_.AnimTime >= 0f)
        {
            log("lifecycle-test SKIP: global clock: -wmvAnimTime pins the clock, so LateUpdate never advances it");
            return;
        }
        const BindingFlags instance = BindingFlags.Instance | BindingFlags.NonPublic;
        const BindingFlags statics = BindingFlags.Static | BindingFlags.NonPublic;
        MethodInfo lateUpdate = typeof(WmvM2Animator).GetMethod("LateUpdate", instance);
        FieldInfo lastRealtime = typeof(WmvM2Animator).GetField("lastRealtime", instance);
        FieldInfo advancedFrame = typeof(WmvM2Animator).GetField("globalAdvancedFrame", statics);
        FieldInfo lastGlobalRealtime = typeof(WmvM2Animator).GetField("lastGlobalRealtime", statics);
        Check(lateUpdate != null && lastRealtime != null && advancedFrame != null && lastGlobalRealtime != null,
              "global clock: LateUpdate and the three clock fields are reachable by reflection", log);
        if (lateUpdate == null || lastRealtime == null || advancedFrame == null || lastGlobalRealtime == null)
            return;

        byte[] m2 = M2Synthetic.InFileSkeletonModel(473370);
        M2ParsedSkin skin = M2SkinParser.Parse(M2Synthetic.TransformSwitchSkin());
        WmvRuntimeModel a = WmvModelBuilder.Build(M2Parser.Parse(m2, 0), skin, new Dictionary<int, BlpImage>(),
                                                  "GlobalClockA", s => log("  " + s));
        WmvRuntimeModel b = WmvModelBuilder.Build(M2Parser.Parse(m2, 0), skin, new Dictionary<int, BlpImage>(),
                                                  "GlobalClockB", s => log("  " + s));
        bool built = a != null && b != null && a.Animator != null && b.Animator != null;
        Check(built, "global clock: two animated models built", log);
        if (!built)
        {
            if (a != null) a.Dispose();
            if (b != null) b.Dispose();
            return;
        }

        double globalWas = WmvM2Animator.GlobalTimeMs;
        object stampWas = advancedFrame.GetValue(null);
        object readingWas = lastGlobalRealtime.GetValue(null);
        try
        {
            // The readings are put in the past, and a reading below zero means "no reading yet" to the
            // animator. This runs from Awake, a fraction of a second after the player started, when
            // 0.4 s ago is still below zero; wait until the past the test needs exists.
            double wait = 2.0 - Time.realtimeSinceStartupAsDouble;
            if (wait > 0.0)
                System.Threading.Thread.Sleep((int)(wait * 1000.0) + 1);

            WmvM2Animator.GlobalTimeMs = 0.0;
            advancedFrame.SetValue(null, Time.frameCount - 1);
            double now = Time.realtimeSinceStartupAsDouble;
            lastGlobalRealtime.SetValue(null, now - 0.4);
            lastRealtime.SetValue(a.Animator, 0.0);
            lastRealtime.SetValue(b.Animator, now - 0.4);
            double bBefore = b.Animator.TimeMs;

            lateUpdate.Invoke(a.Animator, null);
            double afterA = WmvM2Animator.GlobalTimeMs;
            lateUpdate.Invoke(b.Animator, null);
            double afterB = WmvM2Animator.GlobalTimeMs;

            Check(afterA >= 399.0,
                  "global clock: the frame's first animator advances it by the elapsed time (" + afterA.ToString("F1") + " ms)", log);
            Check(afterA < 1000.0,
                  "global clock: ... measured by the global clock's own reading, not by that animator's " +
                  (now * 1000.0).ToString("F0") + " ms away (" + afterA.ToString("F1") + " ms)", log);
            Check(afterB == afterA,
                  "global clock: a second animator in the SAME frame does not advance it again (" + afterB.ToString("F1") +
                  " ms, not about twice that)", log);
            double bMoved = b.Animator.TimeMs - bBefore;
            if (bMoved < 0.0) bMoved += b.Animator.LengthMs;
            Check(bMoved >= 399.0,
                  "global clock: ... while that animator's own sequence clock did advance, so its LateUpdate ran (" +
                  bMoved.ToString("F1") + " ms)", log);

            advancedFrame.SetValue(null, Time.frameCount - 1);
            lastGlobalRealtime.SetValue(null, Time.realtimeSinceStartupAsDouble - 0.4);
            lastRealtime.SetValue(b.Animator, Time.realtimeSinceStartupAsDouble - 0.4);
            lateUpdate.Invoke(b.Animator, null);
            Check(WmvM2Animator.GlobalTimeMs - afterB >= 399.0,
                  "global clock: the next frame advances it again (" + WmvM2Animator.GlobalTimeMs.ToString("F1") + " ms)", log);
        }
        finally
        {
            WmvM2Animator.GlobalTimeMs = globalWas;
            advancedFrame.SetValue(null, stampWas);
            lastGlobalRealtime.SetValue(null, readingWas);
        }
        a.Dispose();
        b.Dispose();
    }

    /// <summary>
    /// WHERE AN ATTACHED ITEM HANGS (WmvCharacterDresser.AttachmentLocalPosition, which Place uses). The
    /// host multiplies the character's bone matrix by a translation to the attachment position; a Unity
    /// bone stands at its pivot, so the same point is the position less the pivot in the bone's space.
    /// Checked on the leaf of a three-bone chain, so the offsets have to telescope through two parents.
    /// </summary>
    static void AttachmentPlacementTests(Action<string> log)
    {
        M2ParsedModel model = M2Parser.Parse(M2Synthetic.InFileSkeletonModel(473370), 0);
        // NoAnimation on an ordinary skinned build: no animator, so the bones stay exactly at rest.
        WmvRuntimeModel body = WmvModelBuilder.Build(model, M2SkinParser.Parse(M2Synthetic.TransformSwitchSkin()),
                                                     new Dictionary<int, BlpImage>(), "AttachmentBody",
                                                     s => log("  " + s), null, null,
                                                     new WmvBuildOptions { NoAnimation = true });
        bool built = body != null && body.Skinned && body.Bones.Length == 3;
        Check(built, "attachment: skinned three-bone body built", log);
        if (!built) { if (body != null) body.Dispose(); return; }
        Check(body.Animator == null && body.Emitters == null && body.Root.GetComponent<WmvM2Animator>() == null,
              "attachment: NoAnimation on a skinned build leaves no animator and no emitters", log);

        M2AttachmentDef att;
        bool found = M2Parser.AttachmentFor(model, 11, out att);
        Check(found && att.Bone == 2, "attachment: id 11 resolves to bone 2, under two parents", log);
        if (!found || att.Bone != 2) { body.Dispose(); return; }

        Vector3 local = WmvCharacterDresser.AttachmentLocalPosition(att.Position, model.Bones[att.Bone].Pivot);
        Check(NearV(local, UnityPosition(att.Position) - UnityPosition(model.Bones[att.Bone].Pivot), 1e-5f),
              "attachment: local position = Convert(attachment position) - Convert(bone pivot)", log);

        Transform probe = new GameObject("AttachmentProbe").transform;
        probe.SetParent(body.Bones[att.Bone], false);
        probe.localPosition = local;
        Check(NearV(probe.position, UnityPosition(att.Position), 1e-4f),
              "attachment: parented under the bone at rest, it sits on the attachment point", log);

        body.Root.transform.position = new Vector3(3f, -1f, 2f);
        body.Root.transform.rotation = Quaternion.Euler(0f, 90f, 0f);
        Check(NearV(probe.position, body.Root.transform.TransformPoint(UnityPosition(att.Position)), 1e-4f),
              "attachment: ... and moves with the character's root", log);

        body.Dispose();         // the probe is under the body's bones and goes with them
    }

    // Exercise the actual equipment lifecycle on an ordinary NPC body, including invalid points.
    static void NpcEquipmentTests(Action<string> log)
    {
        var parsed = WmvIpcClient.ParseCharacterScene("{\"type\":\"characterScene\",\"fileDataID\":4198151," +
            "\"revision\":1,\"load\":17,\"attachmentsOnly\":true,\"attachments\":[]}");
        Check(parsed.attachmentsOnly && parsed.load == 17 && parsed.attachments.Length == 0,
              "NPC equipment: protocol preserves mode and load without a racial body payload", log);
        const int skinId = 990001, itemId = 990002, otherItemId = 990003;
        int live = WmvRuntimeModel.Live;
        var ipcObject = new GameObject("NpcEquipmentTestIpc");
        var ipc = ipcObject.AddComponent<WmvIpcClient>();
        byte[] bytes = M2Synthetic.InFileSkeletonModel(skinId);
        var model = M2Parser.Parse(bytes, 0);
        var body = WmvModelBuilder.Build(model, M2SkinParser.Parse(M2Synthetic.TransformSwitchSkin()),
                                        new Dictionary<int, BlpImage>(), "NpcBody", log);
        var animator = body.Animator;
        var material = body.Materials[0];
        var messages = new List<string>();
        var dresser = new WmvCharacterDresser(ipc, 17, hash => null, s => messages.Add(s));
        var files = (Dictionary<int, byte[]>)typeof(WmvCharacterDresser)
            .GetField("files", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(dresser);
        files[skinId] = M2Synthetic.TransformSwitchSkin();
        files[itemId] = bytes;
        files[otherItemId] = bytes;
        var left = new WmvIpcClient.SceneAttachment { key = "left", fileDataID = itemId,
            attachmentId = 2, bone = 2, position = new[] { 1f, 2f, 3f }, scale = 1f, visible = true };
        var right = new WmvIpcClient.SceneAttachment { key = "right", fileDataID = itemId,
            attachmentId = 1, bone = 1, position = new[] { 2f, 3f, 4f }, scale = 1f, visible = true };
        var scene = new WmvIpcClient.CharacterScene { attachmentsOnly = true, load = 17,
            fileDataID = 4198151, revision = 1, attachments = new[] { left, right } };
        try
        {
            dresser.BeginAttachments(body, model, scene.fileDataID, scene);
            Check(dresser.PartCount == 2 && dresser.AppliedRevision == 1 && WmvRuntimeModel.Live == live + 3,
                  "NPC equipment: both hands built exactly once", log);
            var parts = (System.Collections.IDictionary)typeof(WmvCharacterDresser)
                .GetField("parts", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(dresser);
            object part = parts["left"];
            var weapon = (WmvRuntimeModel)part.GetType().GetField("Runtime").GetValue(part);
            Transform root = weapon.Root.transform;
            Vector3 position = root.position;
            body.Bones[2].localPosition += new Vector3(0.5f, 0f, 0f);
            Check(root.parent == body.Bones[2] && !NearV(position, root.position, 1e-5f),
                  "NPC equipment: weapon follows its attachment bone", log);
            left.fileDataID = otherItemId;
            scene.revision++;
            dresser.Retarget(scene);
            Check(dresser.PartCount == 2 && WmvRuntimeModel.Live == live + 3,
                  "NPC equipment: replacing one hand leaves no duplicate", log);
            left.bone = -1;
            scene.revision++;
            dresser.Retarget(scene);
            Check(dresser.PartCount == 1 && WmvRuntimeModel.Live == live + 2 &&
                  messages.Exists(s => s.Contains("no valid attachment bone/position")),
                  "NPC equipment: absent point removes old item and reports it without root fallback", log);
            left.bone = 9999;
            scene.revision++;
            dresser.Retarget(scene);
            Check(dresser.PartCount == 1, "NPC equipment: out-of-range bone is safe", log);
            left.bone = 2;
            left.position[0] = float.NaN;
            scene.revision++;
            dresser.Retarget(scene);
            Check(dresser.PartCount == 1, "NPC equipment: non-finite attachment position is safe", log);
            scene.attachments = new WmvIpcClient.SceneAttachment[0];
            scene.revision++;
            dresser.Retarget(scene);
            Check(dresser.PartCount == 0 && WmvRuntimeModel.Live == live + 1,
                  "NPC equipment: removal leaves only the body", log);
            Check(body.Animator == animator && body.Materials[0] == material && dresser.BodyRebinds == 0,
                  "NPC equipment: original body material and animator are preserved", log);
        }
        finally
        {
            dresser.Dispose();
            body.Dispose();
            UnityEngine.Object.Destroy(ipcObject);
        }
        Check(WmvRuntimeModel.Live == live, "NPC equipment: disposal releases all runtimes", log);
    }

    // ---------------------------------------------------------------- mounted characters

    /// <summary>
    /// A CHARACTER RIDING A MOUNT (WmvMountedScene): the character's body root hangs from a bone of a second,
    /// separately animated model, and every joint of that is checked on synthetic models built by the real builder --
    /// the skinning of a body under an animated ancestor, the scene's mount on the wire, the three placements, and
    /// the lifecycle of the mount beside the character (mount, swap, dismount, disposal, a load's own mount).
    /// </summary>
    static void MountTests(Action<string> log)
    {
        SkinnedUnderAnimatedBoneTests(log);
        MountSceneJsonTests(log);
        MountPlacementTests(log);
        MountLifecycleTests(log);
        MountAnimationJsonTests(log);
        MountRoutingTests(log);
        MountClockTests(log);
        MountFramingTests(log);
    }

    /// <summary>
    /// A SKINNED BODY UNDER AN ANIMATED BONE OF ANOTHER MODEL stays correct: what a mounted character is. The body is
    /// posed and baked at the world origin first; then it is parented under the leaf bone of a second model whose
    /// sequence and global clock move and turn that bone, both models are posed again, and the bake is compared with
    /// the at-origin bake carried through the parent bone's matrix and the body root's local offset and scale. The
    /// skinning must come out the same in the body's own space and exactly there in the world, at two instants of the
    /// parent's clock and at two scales.
    /// </summary>
    static void SkinnedUnderAnimatedBoneTests(Action<string> log)
    {
        if (WmvModelBuilder.Debug_.NoAnim)
        {
            log("lifecycle-test SKIP: skinning under an animated bone: -wmvNoAnim builds no animator to pose either model");
            return;
        }
        byte[] m2 = M2Synthetic.InFileSkeletonModel(473370);
        M2ParsedSkin skin = M2SkinParser.Parse(M2Synthetic.TransformSwitchSkin());
        M2ParsedModel parentModel = M2Parser.Parse(m2, 0);
        WmvRuntimeModel parent = WmvModelBuilder.Build(parentModel, skin, new Dictionary<int, BlpImage>(), "AncestorMount", null);
        // The body's three vertices on the turning bone and its child, so its own pose rotates as well as moves them.
        M2ParsedModel childModel = M2Parser.Parse(m2, 0);
        Influence(childModel, 0, 2, 255, 0, 0);
        Influence(childModel, 1, 1, 255, 0, 0);
        Influence(childModel, 2, 1, 128, 2, 127);
        WmvRuntimeModel child = WmvModelBuilder.Build(childModel, skin, new Dictionary<int, BlpImage>(), "AncestorRider", null);
        bool built = parent != null && child != null && parent.Animator != null && child.Animator != null &&
                     child.Skin != null && parent.Bones.Length == 3;
        Check(built, "skinning under an animated bone: two skinned, animated three-bone models built", log);
        if (!built)
        {
            if (parent != null) parent.Dispose();
            if (child != null) child.Dispose();
            return;
        }

        M2AttachmentDef att;
        bool found = M2Parser.AttachmentFor(parentModel, 11, out att) && att.Bone == 2;
        Check(found, "skinning under an animated bone: the parent's attachment 11 is on its leaf bone 2", log);
        if (!found) { child.Dispose(); parent.Dispose(); return; }
        Vector3 offset = WmvCharacterDresser.AttachmentLocalPosition(att.Position, parentModel.Bones[att.Bone].Pivot);
        Transform bone = parent.Bones[att.Bone];

        double globalWas = WmvM2Animator.GlobalTimeMs;
        var baked = new Mesh();
        try
        {
            // Bone 1 turns on global sequence 1: at 250 ms it has turned 90 degrees, in both models.
            WmvM2Animator.GlobalTimeMs = 250.0;
            child.Animator.ApplyPose(250f);
            child.Skin.BakeMesh(baked, true);
            Vector3[] atOrigin = baked.vertices;
            Vector3[] file = child.Mesh.vertices;
            bool posed = atOrigin.Length == 3 && file.Length == 3;
            float moved = 0f;
            for (int i = 0; posed && i < 3; i++)
                moved = Mathf.Max(moved, Vector3.Distance(atOrigin[i], file[i]));
            Check(posed && moved > 0.1f,
                  "skinning under an animated bone: the body's own pose moves its vertices at the origin (" + moved.ToString("F3") + ")", log);

            Vector3[] firstWorld = null;
            foreach (float scale in new[] { 1f, 1.5f })
            {
                foreach (float parentMs in new[] { 125f, 375f })
                {
                    string at = "parent at " + parentMs + " ms, body scale " + scale;
                    Transform root = child.Root.transform;
                    root.SetParent(bone, false);
                    root.localPosition = offset;
                    root.localRotation = Quaternion.identity;
                    root.localScale = new Vector3(scale, scale, scale);
                    parent.Animator.ApplyPose(parentMs);
                    child.Animator.ApplyPose(250f);

                    Check(!SameRotation(bone.rotation, Quaternion.identity) &&
                          !NearV(bone.position, UnityPosition(parentModel.Bones[att.Bone].Pivot), 1e-3f),
                          "skinning under an animated bone: " + at + ": the parent bone is turned and moved from its rest", log);

                    child.Skin.BakeMesh(baked, true);
                    Vector3[] local = baked.vertices;
                    Matrix4x4 bodyToWorld = root.localToWorldMatrix;
                    Matrix4x4 expectedToWorld = bone.localToWorldMatrix *
                                                Matrix4x4.TRS(offset, Quaternion.identity, new Vector3(scale, scale, scale));
                    bool sameLocal = local.Length == atOrigin.Length;
                    float worst = 0f;
                    var world = new Vector3[local.Length];
                    for (int i = 0; sameLocal && i < local.Length; i++)
                    {
                        sameLocal = NearV(local[i], atOrigin[i], 1e-3f);
                        world[i] = bodyToWorld.MultiplyPoint3x4(local[i]);
                        worst = Mathf.Max(worst, Vector3.Distance(world[i], expectedToWorld.MultiplyPoint3x4(atOrigin[i])));
                    }
                    Check(sameLocal,
                          "skinning under an animated bone: " + at + ": the bake in the body's own space is the bake at the origin", log);
                    Check(local.Length == atOrigin.Length && worst < 1e-3f,
                          "skinning under an animated bone: " + at + ": in the world it is the at-origin bake carried through the " +
                          "parent bone and the body's offset (largest difference " + worst.ToString("E2") + ")", log);
                    if (firstWorld == null)
                        firstWorld = world;
                    else if (scale == 1f)
                        Check(!NearV(world[0], firstWorld[0], 1e-2f),
                              "skinning under an animated bone: the body follows the parent's clock (a different instant puts it elsewhere)", log);
                }
            }
        }
        finally
        {
            WmvM2Animator.GlobalTimeMs = globalWas;
            UnityEngine.Object.Destroy(baked);
        }
        child.Root.transform.SetParent(null, false);
        child.Dispose();
        parent.Dispose();
    }

    /// <summary>
    /// A characterScene LINE with and without a mount, parsed exactly as the IPC client parses it: presence is the
    /// sentinel (a non-empty key and a fileDataID), never whether the nested object came back null -- which is
    /// logged, since it was not established what JsonUtility does with an absent one.
    /// </summary>
    static void MountSceneJsonTests(Action<string> log)
    {
        const string head = "{\"type\":\"characterScene\",\"fileDataID\":1011653,\"revision\":4," +
                            "\"body\":{\"textures\":[],\"submeshCount\":0,\"submeshVisible\":[]},\"merged\":[],\"attachments\":[]";
        WmvIpcClient.CharacterScene without = WmvIpcClient.ParseCharacterScene(head + "}");
        Check(without != null && without.fileDataID == 1011653 && without.revision == 4,
              "mount json: a characterScene line without a mount parses", log);
        Check(without != null && !WmvIpcClient.HasMount(without) && WmvIpcClient.MountKeyOf(without) == "",
              "mount json: ... has no mount by the sentinel, and answers mountKey \"\"", log);
        if (without != null)
            log("lifecycle-test INFO: mount json: an absent \"mount\" comes back as " +
                (without.mount == null ? "null"
                 : "a default-filled object (key " + (without.mount.key == null ? "null" : "\"" + without.mount.key + "\"") +
                   ", fileDataID " + without.mount.fileDataID + ")"));

        const string mount = ",\"mount\":{\"key\":\"M3\",\"fileDataID\":126407,\"path\":\"creature/warhorse/warhorse.m2\"," +
                             "\"displayID\":8469,\"attachmentId\":0,\"bone\":50,\"position\":[0.2131,0.0,1.9284],\"riderScale\":1.0," +
                             "\"textures\":[{\"slot\":0,\"type\":11,\"fileDataID\":126406}],\"submeshCount\":2,\"submeshVisible\":[1,0]," +
                             "\"sequenceIndex\":1,\"riderSequenceIndex\":145}";
        WmvIpcClient.CharacterScene with = WmvIpcClient.ParseCharacterScene(head + mount + "}");
        bool has = with != null && WmvIpcClient.HasMount(with);
        Check(has, "mount json: the same line with a mount has one by the sentinel", log);
        if (has)
        {
            WmvIpcClient.SceneMount m = with.mount;
            Check(m.key == "M3" && m.fileDataID == 126407 && m.path == "creature/warhorse/warhorse.m2" && m.displayID == 8469 &&
                  m.attachmentId == 0 && m.bone == 50 && m.sequenceIndex == 1 && m.riderSequenceIndex == 145,
                  "mount json: key, file, path, display, attachment id, bone and both sequence indices are read", log);
            Check(m.position != null && m.position.Length == 3 && Near(m.position[0], 0.2131f) && Near(m.position[1], 0f) &&
                  Near(m.position[2], 1.9284f) && Near(m.riderScale, 1f),
                  "mount json: the attachment position and the rider scale are read", log);
            Check(m.textures != null && m.textures.Length == 1 && m.textures[0].slot == 0 && m.textures[0].type == 11 &&
                  m.textures[0].fileDataID == 126406, "mount json: the mount's texture slot is read", log);
            bool[] flags = WmvIpcClient.Flags(m.submeshVisible);
            Check(m.submeshCount == 2 && flags.Length == 2 && flags[0] && !flags[1], "mount json: the mount's geoset flags are read", log);
            Check(WmvMountedScene.ParticleSets(m.particleColorSets) == null,
                  "mount json: no particleColorSets is no colour replacement (the authored colours stay)", log);
            Check(WmvIpcClient.MountKeyOf(with) == "M3", "mount json: ... and its key is what the answer names", log);
        }

        WmvIpcClient.CharacterScene noKey = WmvIpcClient.ParseCharacterScene(head + ",\"mount\":{\"key\":\"\",\"fileDataID\":126407}}");
        Check(noKey != null && !WmvIpcClient.HasMount(noKey), "mount json: a mount with an empty key is no mount", log);
        WmvIpcClient.CharacterScene noFile = WmvIpcClient.ParseCharacterScene(head + ",\"mount\":{\"key\":\"M4\",\"fileDataID\":0}}");
        Check(noFile != null && !WmvIpcClient.HasMount(noFile) && WmvIpcClient.MountKeyOf(noFile) == "M4",
              "mount json: a key without a fileDataID is no mount the player can build, and its key is still answered", log);

        // The host's 27 numbers: set 0..2 (emitter ParticleColorIndex 11..13), start/mid/end, r,g,b.
        var values = new int[27];
        for (int i = 0; i < values.Length; i++) values[i] = i * 9;
        Color[][] sets = WmvMountedScene.ParticleSets(values);
        Check(sets != null && sets.Length == 3 && sets[0].Length == 3 && Near(sets[0][0].r, 0f) && Near(sets[0][1].r, 27f / 255f) &&
              Near(sets[1][2].b, 153f / 255f) && Near(sets[2][2].b, 234f / 255f) && Near(sets[2][0].a, 1f),
              "mount json: particleColorSets become set i's start, mid and end colours, in the host's order", log);
        Check(WmvMountedScene.ParticleSets(new int[26]) == null, "mount json: fewer than 27 numbers is no replacement", log);
    }

    /// <summary>
    /// THE THREE PLACEMENTS (WmvMountedScene.Placement), on a skinned mount with bones and a static one without:
    /// no attachment -> the mount's root at zero; a bone with a Transform -> that bone at the attachment position less
    /// its pivot; a bone with no Transform in the build -> the mount's root at the converted position, which is not the
    /// zero of the first case.
    /// </summary>
    static void MountPlacementTests(Action<string> log)
    {
        M2ParsedSkin skin = M2SkinParser.Parse(M2Synthetic.TransformSwitchSkin());
        M2ParsedModel model = M2Parser.Parse(M2Synthetic.InFileSkeletonModel(473370), 0);
        WmvRuntimeModel mount = WmvModelBuilder.Build(model, skin, new Dictionary<int, BlpImage>(), "PlacementMount", null, null, null,
                                                      new WmvBuildOptions { NoAnimation = true });
        M2ParsedModel flatModel = M2Parser.Parse(M2Synthetic.TransformSwitchModel(0, false), 0);
        WmvRuntimeModel flat = WmvModelBuilder.Build(flatModel, skin, new Dictionary<int, BlpImage>(), "PlacementStatic", null);
        bool built = mount != null && mount.Skinned && mount.Bones.Length == 3 && flat != null && !flat.Skinned && flat.Bones.Length == 0;
        Check(built, "mount placement: a skinned three-bone mount and a static one without bones built", log);
        M2AttachmentDef att;
        bool found = built && M2Parser.AttachmentFor(model, 11, out att) && att.Bone == 2;
        if (!found)
        {
            Check(false, "mount placement: attachment 11 resolves to bone 2", log);
            if (mount != null) mount.Dispose();
            if (flat != null) flat.Dispose();
            return;
        }
        M2Parser.AttachmentFor(model, 11, out att);
        float[] position = { att.Position.X, att.Position.Y, att.Position.Z };

        Transform parent;
        Vector3 local;
        int placement = WmvMountedScene.Placement(-1, position, mount, model, out parent, out local);
        Check(placement == WmvMountedScene.CaseNoAttachment && parent == mount.Root.transform && local == Vector3.zero,
              "mount placement: bone -1 (the mount has no such attachment) -- the mount's root, zero offset (case A)", log);

        placement = WmvMountedScene.Placement(att.Bone, position, mount, model, out parent, out local);
        Check(placement == WmvMountedScene.CaseBone && parent == mount.Bones[att.Bone] &&
              NearV(local, UnityPosition(att.Position) - UnityPosition(model.Bones[att.Bone].Pivot), 1e-5f),
              "mount placement: a bone with a Transform -- that bone, at Convert(position) - Convert(pivot) (case B)", log);
        Transform probe = new GameObject("MountPlacementProbe").transform;
        probe.SetParent(parent, false);
        probe.localPosition = local;
        Check(NearV(probe.position, UnityPosition(att.Position), 1e-4f),
              "mount placement: ... which at rest is exactly the attachment point", log);

        placement = WmvMountedScene.Placement(att.Bone, position, flat, flatModel, out parent, out local);
        Check(placement == WmvMountedScene.CaseNoBoneTransform && parent == flat.Root.transform &&
              NearV(local, UnityPosition(att.Position), 1e-5f) && local != Vector3.zero,
              "mount placement: a bone the static build has no Transform for -- the root at Convert(position) (case C, not case A)", log);

        placement = WmvMountedScene.Placement(7, position, mount, model, out parent, out local);
        Check(placement == WmvMountedScene.CaseNoBoneTransform && parent == mount.Root.transform &&
              NearV(local, UnityPosition(att.Position), 1e-5f),
              "mount placement: a bone past the build's skeleton is case C too", log);

        mount.Dispose();        // the probe is under its bones and goes with them
        flat.Dispose();
    }

    /// <summary>The host's asset channel, for a mounted scene under test: each request gets an id, and Deliver answers
    /// the ones the scene owns -- and the ones its answers lead to -- from the files given, a file not given as missing.</summary>
    class MountAssets
    {
        public readonly Dictionary<int, byte[]> Files = new Dictionary<int, byte[]>();
        public readonly List<KeyValuePair<string, int>> Requests = new List<KeyValuePair<string, int>>();
        readonly HashSet<string> answered = new HashSet<string>();
        int next;

        public string Request(int fileDataID)
        {
            string id = "mount-test-" + (++next);
            Requests.Add(new KeyValuePair<string, int>(id, fileDataID));
            return id;
        }

        public void Deliver(WmvMountedScene scene)
        {
            for (int round = 0; round < 16; round++)
            {
                bool any = false;
                foreach (var kv in Requests.ToArray())
                {
                    if (answered.Contains(kv.Key) || !scene.Owns(kv.Key))
                        continue;
                    answered.Add(kv.Key);
                    any = true;
                    byte[] data;
                    bool ok = Files.TryGetValue(kv.Value, out data);
                    scene.OnAsset(new WmvIpcClient.AssetResponse
                    {
                        requestId = kv.Key, ok = ok, fileDataID = kv.Value, data = ok ? data : null, error = ok ? null : "not served",
                    });
                }
                if (!any)
                    return;
            }
        }
    }

    static WmvIpcClient.SceneMount MountOf(string key, int fileDataID, int bone, float[] position, float riderScale,
                                          int sequence, int riderSequence, int textureFileDataID)
    {
        return new WmvIpcClient.SceneMount
        {
            key = key, fileDataID = fileDataID, path = "mount/test/" + fileDataID + ".m2", attachmentId = 0, bone = bone,
            position = position, riderScale = riderScale, sequenceIndex = sequence, riderSequenceIndex = riderSequence,
            textures = textureFileDataID > 0
                ? new[] { new WmvIpcClient.SceneTexture { slot = 0, type = 0, fileDataID = textureFileDataID } }
                : new WmvIpcClient.SceneTexture[0],
        };
    }

    /// <summary>A chunked M2 of an MD20 image with an SFID naming skinFileId: what a mount fetch needs.</summary>
    static byte[] WithSkinFile(byte[] md20, int skinFileId)
    {
        var b = new byte[8 + md20.Length + 12];
        b[0] = (byte)'M'; b[1] = (byte)'D'; b[2] = (byte)'2'; b[3] = (byte)'1';
        BitConverter.GetBytes(md20.Length).CopyTo(b, 4);
        Buffer.BlockCopy(md20, 0, b, 8, md20.Length);
        int o = 8 + md20.Length;
        b[o] = (byte)'S'; b[o + 1] = (byte)'F'; b[o + 2] = (byte)'I'; b[o + 3] = (byte)'D';
        BitConverter.GetBytes(4).CopyTo(b, o + 4);
        BitConverter.GetBytes(skinFileId).CopyTo(b, o + 8);
        return b;
    }

    /// <summary>A 2x2 palettized BLP2 of one opaque colour, so a mount texture fetched and decoded for real can be told
    /// apart on the material it lands on.</summary>
    static byte[] SolidBlp(byte r, byte g, byte b)
    {
        const int header = 0x494, pixels = 4;
        var f = new byte[header + pixels * 2];
        f[0] = (byte)'B'; f[1] = (byte)'L'; f[2] = (byte)'P'; f[3] = (byte)'2';
        BitConverter.GetBytes(1).CopyTo(f, 0x04);
        f[0x08] = 1;                                           // palettized
        f[0x09] = 8;                                           // 8-bit alpha
        BitConverter.GetBytes(2).CopyTo(f, 0x0C);
        BitConverter.GetBytes(2).CopyTo(f, 0x10);
        BitConverter.GetBytes(header).CopyTo(f, 0x14);         // mip 0 offset
        BitConverter.GetBytes(pixels * 2).CopyTo(f, 0x54);     // mip 0 size
        f[0x94 + 4] = b; f[0x94 + 5] = g; f[0x94 + 6] = r;      // palette entry 1, BGRA
        for (int i = 0; i < pixels; i++) { f[header + i] = 1; f[header + pixels + i] = 0xFF; }
        return f;
    }

    /// <summary>
    /// THE MOUNT BESIDE THE CHARACTER, through WmvMountedScene's own fetch, build and commit, answered by MountAssets:
    /// a mount's files and the riding sequence's .anim fetched before it goes on, the same key and file kept in flight,
    /// the character hung from the attachment's bone, the mount's texture bound, the same mount described again not
    /// rebuilt, swaps, dismounts and remounts leaving the live model count where it was and the character alive, a
    /// mount that cannot be built, cases C and A through a commit, disposal taking the character off first, and a load's
    /// own mount beside the one on screen: untouched when the on-screen one goes (AdoptBuilt), committed for the new
    /// character (AdoptStaged), and released with its requests when its load is dropped (AbandonCharacterJob) -- the
    /// calls WmvMain makes, in the order it makes them.
    /// </summary>
    static void MountLifecycleTests(Action<string> log)
    {
        const int mountA = 780001, mountB = 780002, mountStatic = 780004, unfetched = 780005, missing = 780099, skinId = 780003;
        const int superseded = 780006;
        const int red = 780010, blue = 780011;
        int anim = M2Synthetic.SkelAnimFileIdBase;            // the fixture's .anim, for its external sequence 1
        var assets = new MountAssets();
        assets.Files[mountA] = M2Synthetic.InFileSkeletonModel(skinId);
        assets.Files[mountB] = M2Synthetic.InFileSkeletonModel(skinId);
        assets.Files[unfetched] = M2Synthetic.InFileSkeletonModel(skinId);
        assets.Files[superseded] = M2Synthetic.InFileSkeletonModel(skinId);
        assets.Files[mountStatic] = WithSkinFile(M2Synthetic.TransformSwitchModel(0, false), skinId);
        assets.Files[skinId] = M2Synthetic.TransformSwitchSkin();
        assets.Files[anim] = M2Synthetic.SkeletonAnimFile(0, true);
        assets.Files[red] = SolidBlp(255, 0, 0);
        assets.Files[blue] = SolidBlp(0, 0, 255);
        float[] seat = { 0.5f, 0f, 1.5f };                      // the fixture's attachment 11, on bone 2
        int external = M2Synthetic.SkelExternalSequence;

        int liveAtStart = WmvRuntimeModel.Live;
        int mountsAtStart = WmvMountedScene.LiveMounts, builtAtStart = WmvMountedScene.MountsBuilt;
        M2ParsedSkin skin = M2SkinParser.Parse(M2Synthetic.TransformSwitchSkin());
        M2ParsedModel riderModel = M2Parser.Parse(M2Synthetic.InFileSkeletonModel(473370), 0);
        WmvRuntimeModel rider = WmvModelBuilder.Build(riderModel, skin, new Dictionary<int, BlpImage>(), "MountRider", null);
        var riderSlot = new WmvModelSlot { Runtime = rider, Model = riderModel, FileDataID = 473370 };
        int live = WmvRuntimeModel.Live;
        int progress = 0;
        var scene = new WmvMountedScene(assets.Request, s => log("  " + s), () => progress++);

        // ---- a mount going on: its files, the riding sequence's .anim, then the commit ----
        WmvIpcClient.SceneMount m1 = MountOf("M1", mountA, 2, seat, 1f, external, external, red);
        scene.Retarget(m1);
        Check(!scene.ReadyFor(m1, riderSlot, riderModel), "mount lifecycle: not ready before the mount's files are here", log);
        int asked = assets.Requests.Count;
        scene.Retarget(MountOf("M1", mountA, 2, seat, 1f, external, external, red));
        Check(assets.Requests.Count == asked,
              "mount lifecycle: the same key and file described again keeps the work in flight -- nothing is asked twice", log);
        assets.Deliver(scene);
        Check(progress > 0, "mount lifecycle: each answer taken in reports progress, so a waiting scene looks again", log);
        Check(riderSlot.AnimFileCache.ContainsKey(anim),
              "mount lifecycle: the .anim of the character's riding sequence landed in the character's slot", log);
        Check(scene.Staged != null && scene.Staged.Root != null && !scene.Staged.Root.activeSelf && scene.RiddenFileDataID == 0,
              "mount lifecycle: the mount is built, inactive and not ridden before the character's scene is applied", log);
        Check(scene.ReadyFor(m1, riderSlot, riderModel), "mount lifecycle: ... and then the scene may be applied", log);
        Check(WmvRuntimeModel.Live == live + 1, "mount lifecycle: one runtime more, the staged mount", log);
        Check(WmvMountedScene.LiveMounts == mountsAtStart + 1 && WmvMountedScene.MountsBuilt == builtAtStart + 1 &&
              scene.SeatCase == -1 && scene.SeatBone == -1,
              "mount lifecycle: ... counted as one mount alive and one built, with nobody seated yet (runtimeState)", log);

        bool newMount;
        WmvIpcClient.MountAnswer answer = scene.Commit(m1, rider, out newMount);
        Transform body = rider.Root.transform;
        WmvRuntimeModel onScreen = scene.Mount.Runtime;
        Check(answer.Status == "applied" && answer.Key == "M1" && answer.Reason == "" && newMount,
              "mount lifecycle: committed -- applied, key M1, a new mount (both sequences start)", log);
        bool up = onScreen != null && onScreen.Root.activeSelf && scene.Key == "M1" && scene.RiddenFileDataID == mountA;
        Check(up, "mount lifecycle: the mount is on screen, active, and ridden", log);
        if (!up)
        {
            scene.Dispose();
            rider.Dispose();
            return;
        }
        Check(body.parent == onScreen.Bones[2] && scene.Rider == body,
              "mount lifecycle: the character's body root hangs from the attachment's bone (case B)", log);
        Check(scene.SeatCase == WmvMountedScene.CaseBone && scene.SeatBone == 2 && scene.SeatScale == 1f &&
              NearV(scene.SeatLocalPosition, body.localPosition, 1e-6f) && WmvMountedScene.LiveMounts == mountsAtStart + 1,
              "mount lifecycle: ... and the scene reports that seat (case B, bone 2, its local position and scale), one mount alive", log);
        Check(NearV(body.localPosition, WmvCharacterDresser.AttachmentLocalPosition(new WowVec3(0.5f, 0f, 1.5f),
                                                                                    scene.Mount.Model.Bones[2].Pivot), 1e-5f) &&
              SameRotation(body.localRotation, Quaternion.identity) && NearV(body.localScale, Vector3.one, 1e-6f),
              "mount lifecycle: ... at the attachment position less the bone's pivot, identity rotation, the rider scale 1", log);
        Check(scene.Mount.Model.AnimatedSequence == external && scene.Mount.AnimFileCache.ContainsKey(anim) &&
              onScreen.Animator != null && onScreen.Animator.SequenceIndex == external,
              "mount lifecycle: the mount plays the host's sequence, whose keys were in a .anim, from the start", log);
        Check(onScreen.Materials.Length > 0 && BaseColour(onScreen.Materials[0]) == 'r' && scene.Mount.TextureIds[0] == red,
              "mount lifecycle: the host's texture for slot 0 is bound on the mount", log);

        // ---- the same mount described again: new texture, rider scale 2 ----
        WmvIpcClient.SceneMount m1b = MountOf("M1", mountA, 2, seat, 2f, external, external, blue);
        scene.Retarget(m1b);
        Check(!scene.ReadyFor(m1b, riderSlot, riderModel), "mount lifecycle: a newer description waits for its new texture", log);
        assets.Deliver(scene);
        Check(scene.ReadyFor(m1b, riderSlot, riderModel), "mount lifecycle: ... which then arrives", log);
        answer = scene.Commit(m1b, rider, out newMount);
        Check(answer.Status == "applied" && !newMount && scene.Mount.Runtime == onScreen && WmvRuntimeModel.Live == live + 1 &&
              WmvMountedScene.MountsBuilt == builtAtStart + 1 && WmvMountedScene.LiveMounts == mountsAtStart + 1,
              "mount lifecycle: the same mount described again is applied without a rebuild (none counted) or a sequence restart", log);
        Check(BaseColour(onScreen.Materials[0]) == 'b', "mount lifecycle: ... its new texture re-bound on the materials it has", log);
        Check(NearV(body.localScale, new Vector3(2f, 2f, 2f), 1e-6f) && body.parent == onScreen.Bones[2],
              "mount lifecycle: ... and the character re-seated at the new rider scale", log);

        // ---- swaps: the character moves onto the new mount, the old one goes after it left ----
        bool swapsOk = true;
        for (int n = 0; n < 4; n++)
        {
            WmvIpcClient.SceneMount m = MountOf("M" + (10 + n), n % 2 == 0 ? mountB : mountA, 2, seat, 1f, 0, external, red);
            WmvRuntimeModel old = scene.Mount.Runtime;
            Transform oldRoot = old.Root.transform;
            scene.Retarget(m);
            assets.Deliver(scene);
            swapsOk &= scene.ReadyFor(m, riderSlot, riderModel);
            answer = scene.Commit(m, rider, out newMount);
            swapsOk &= answer.Status == "applied" && answer.Key == m.key && newMount && scene.Mount.Runtime != old && old.Root == null;
            swapsOk &= body.parent == scene.Mount.Runtime.Bones[2] && !body.IsChildOf(oldRoot) && rider.Root != null;
            swapsOk &= WmvRuntimeModel.Live == live + 1 && scene.RiddenFileDataID == m.fileDataID;
            swapsOk &= WmvMountedScene.LiveMounts == mountsAtStart + 1 && WmvMountedScene.MountsBuilt == builtAtStart + 2 + n &&
                       scene.SeatCase == WmvMountedScene.CaseBone;
        }
        Check(swapsOk, "mount lifecycle: four swaps -- a new key each, the character moved onto each new mount before the " +
                       "old one was disposed, one mount alive throughout (and counted so), one built per swap", log);

        // ---- dismount and remount ----
        WmvRuntimeModel last = scene.Mount.Runtime;
        answer = scene.Dismount();
        Check(answer.Status == "none" && answer.Key == "" && scene.RiddenFileDataID == 0 && scene.Mount.Runtime == null &&
              last.Root == null, "mount lifecycle: dismounted -- the answer is none, the mount disposed", log);
        Check(body.parent == null && body.localPosition == Vector3.zero && SameRotation(body.localRotation, Quaternion.identity) &&
              NearV(body.localScale, Vector3.one, 1e-6f) && WmvRuntimeModel.Live == live,
              "mount lifecycle: ... the character is off it with the identity, and the live count is back", log);
        Check(scene.SeatCase == -1 && scene.SeatBone == -1 && WmvMountedScene.LiveMounts == mountsAtStart,
              "mount lifecycle: ... nobody seated and no mount alive, as runtimeState reports it", log);
        bool cyclesOk = true;
        for (int n = 0; n < 3; n++)
        {
            WmvIpcClient.SceneMount m = MountOf("M" + (20 + n), mountA, 2, seat, 1f, 0, external, red);
            scene.Retarget(m);
            assets.Deliver(scene);
            answer = scene.Commit(m, rider, out newMount);
            cyclesOk &= answer.Status == "applied" && body.parent == scene.Mount.Runtime.Bones[2] && WmvRuntimeModel.Live == live + 1;
            answer = scene.Dismount();
            cyclesOk &= answer.Status == "none" && body.parent == null && WmvRuntimeModel.Live == live;
        }
        Check(cyclesOk && rider.Root != null && rider.Animator != null && rider.Skin != null,
              "mount lifecycle: three mount/dismount cycles leave the live count unchanged and the character's body alive", log);

        // ---- a mount that cannot be built ----
        WmvIpcClient.SceneMount ok = MountOf("M30", mountA, 2, seat, 1f, 0, external, red);
        scene.Retarget(ok);
        assets.Deliver(scene);
        scene.Commit(ok, rider, out newMount);
        WmvRuntimeModel beforeFailure = scene.Mount.Runtime;
        WmvIpcClient.SceneMount bad = MountOf("M31", missing, 2, seat, 1f, 0, external, 0);
        scene.Retarget(bad);
        assets.Deliver(scene);
        Check(scene.ReadyFor(bad, riderSlot, riderModel), "mount lifecycle: a mount whose file cannot be read is ready to be answered", log);
        answer = scene.Commit(bad, rider, out newMount);
        Check(answer.Status == "failed" && answer.Key == "M31" && answer.Reason.Length > 0 && newMount,
              "mount lifecycle: ... answered failed, with its reason (" + answer.Reason + ")", log);
        Check(body.parent == null && scene.RiddenFileDataID == 0 && beforeFailure.Root == null && WmvRuntimeModel.Live == live,
              "mount lifecycle: ... the character stays on screen off any mount, and the mount it rode is gone", log);

        // ---- cases C and A through a commit ----
        WmvIpcClient.SceneMount still = MountOf("M40", mountStatic, 2, seat, 1f, 0, external, red);
        scene.Retarget(still);
        assets.Deliver(scene);
        answer = scene.Commit(still, rider, out newMount);
        Check(answer.Status == "applied" && scene.Mount.Runtime != null && scene.Mount.Runtime.Bones.Length == 0 &&
              body.parent == scene.Mount.Runtime.Root.transform && NearV(body.localPosition, UnityPosition(new WowVec3(0.5f, 0f, 1.5f)), 1e-5f),
              "mount lifecycle: a static mount with a bone named -- the character at the converted position on its root (case C)", log);
        Check(scene.SeatCase == WmvMountedScene.CaseNoBoneTransform && scene.SeatBone == -1,
              "mount lifecycle: ... reported as case C, on no bone", log);
        WmvIpcClient.SceneMount none = MountOf("M41", mountA, -1, new float[] { 0f, 0f, 0f }, 1f, 0, external, red);
        scene.Retarget(none);
        assets.Deliver(scene);
        answer = scene.Commit(none, rider, out newMount);
        Check(answer.Status == "applied" && body.parent == scene.Mount.Runtime.Root.transform && body.localPosition == Vector3.zero,
              "mount lifecycle: a mount with no such attachment -- the character at its origin (case A)", log);
        Check(scene.SeatCase == WmvMountedScene.CaseNoAttachment && scene.SeatBone == -1 && WmvMountedScene.LiveMounts == mountsAtStart + 1,
              "mount lifecycle: ... reported as case A, on no bone, one mount alive", log);

        // ---- a superseded target drops the work, not the answers still owed to it ----
        int askedBeforeCancel = assets.Requests.Count;
        scene.Retarget(MountOf("M43", superseded, 2, seat, 1f, 0, external, red));
        string owed = null;
        foreach (var kv in assets.Requests) if (scene.Owns(kv.Key)) owed = kv.Key;
        scene.CancelTarget();
        Check(owed != null && scene.Owns(owed) && WmvMountedScene.LiveMounts == mountsAtStart + 1,
              "mount lifecycle: a cancelled target keeps claiming the answers it is owed, and the mount on screen stays", log);
        int askedAfterCancel = assets.Requests.Count;
        scene.Retarget(MountOf("M44", superseded, 2, seat, 1f, 0, external, red));
        Check(askedAfterCancel > askedBeforeCancel && assets.Requests.Count == askedAfterCancel,
              "mount lifecycle: ... so the same mount described again never asks twice for a file already coming", log);
        scene.CancelTarget();

        var abandoning = new WmvModelSlot();
        abandoning.PendingAnimFetch["mount-test-anim"] = 3;
        abandoning.AbandonAnimFetches();
        Check(abandoning.PendingAnimFetch.Count == 0 && abandoning.AbandonedAnimFetch.Contains("mount-test-anim"),
              "mount lifecycle: a slot given another model stops waiting for its .anim fetch but still claims its answer", log);

        // ---- disposal takes the character off first ----
        Transform mountRoot = scene.Mount.Runtime.Root.transform;
        string pendingId = null;
        scene.Retarget(MountOf("M42", unfetched, 2, seat, 1f, 0, external, red));     // its .m2 still out
        foreach (var kv in assets.Requests) if (scene.Owns(kv.Key)) pendingId = kv.Key;
        scene.Dispose();
        Check(rider.Root != null && body.parent == null && !body.IsChildOf(mountRoot) && WmvRuntimeModel.Live == live,
              "mount lifecycle: disposing a mounted scene takes the character off before the mount goes -- the body is alive", log);
        Check(pendingId == null || !scene.Owns(pendingId), "mount lifecycle: ... and a disposed scene claims no answer", log);

        // ---- a load's own mount beside the one on screen ----
        var shown = new WmvMountedScene(assets.Request, null, null);
        WmvIpcClient.SceneMount s1 = MountOf("M50", mountA, 2, seat, 1f, 0, external, red);
        shown.Retarget(s1);
        assets.Deliver(shown);
        shown.Commit(s1, rider, out newMount);
        M2ParsedModel nextModel = M2Parser.Parse(M2Synthetic.InFileSkeletonModel(473370), 0);
        WmvRuntimeModel next = WmvModelBuilder.Build(nextModel, skin, new Dictionary<int, BlpImage>(), "MountNextRider", null);
        next.Root.SetActive(false);                              // staged, as a loading character's body is
        var ofLoad = new WmvMountedScene(assets.Request, null, null);
        WmvIpcClient.SceneMount s2 = MountOf("M50", mountA, 2, seat, 1f, 0, external, red);   // reconnect: the same key
        ofLoad.Retarget(s2);
        assets.Deliver(ofLoad);
        int liveBoth = WmvRuntimeModel.Live;
        Check(ofLoad.Staged != null && !ofLoad.Staged.Root.activeSelf && shown.Rider == body,
              "load mount: a load's mount is built beside the mount on screen, which still carries the character", log);
        Check(WmvMountedScene.LiveMounts == mountsAtStart + 2 && ofLoad.SeatCase == -1 && shown.SeatCase == WmvMountedScene.CaseBone,
              "load mount: ... both counted alive, the load's with nobody seated on it", log);
        shown.Dispose();                                         // AdoptBuilt: the mount on screen goes with its character
        Check(body.parent == null && ofLoad.Staged != null && ofLoad.Staged.Root != null && WmvRuntimeModel.Live == liveBoth - 1,
              "load mount: the mount on screen is disposed, its character taken off first, and the load's mount survives it", log);
        next.Root.SetActive(true);                               // AdoptStaged, then the dresser's commit
        answer = ofLoad.Commit(s2, next, out newMount);
        Check(answer.Status == "applied" && newMount && next.Root.transform.parent == ofLoad.Mount.Runtime.Bones[2] &&
              ofLoad.Mount.Runtime.Root.activeSelf,
              "load mount: ... becomes the mount on screen with the new character on it, its sequences started", log);
        var dropped = new WmvMountedScene(assets.Request, null, null);
        dropped.Retarget(MountOf("M51", mountB, 2, seat, 1f, 0, external, red));
        string inFlight = null;
        foreach (var kv in assets.Requests) if (dropped.Owns(kv.Key)) inFlight = kv.Key;
        var built = new WmvMountedScene(assets.Request, null, null);
        WmvIpcClient.SceneMount s3 = MountOf("M52", mountA, 2, seat, 1f, 0, external, red);
        built.Retarget(s3);
        assets.Deliver(built);
        int liveDropping = WmvRuntimeModel.Live;
        dropped.Dispose();                                       // AbandonCharacterJob, its mount still being fetched
        built.Dispose();                                         // ... and one whose mount was already built
        Check(inFlight != null && !dropped.Owns(inFlight) && WmvRuntimeModel.Live == liveDropping - 1,
              "load mount: a dropped load's mount releases its requests and its staged build", log);
        if (inFlight != null)
            dropped.OnAsset(new WmvIpcClient.AssetResponse { requestId = inFlight, ok = true, fileDataID = mountB, data = assets.Files[mountB] });
        Check(WmvRuntimeModel.Live == liveDropping - 1 && dropped.Staged == null,
              "load mount: ... and a late answer for it builds nothing", log);

        ofLoad.Dispose();
        next.Dispose();
        rider.Dispose();
        Check(WmvRuntimeModel.Live == liveAtStart, "mount lifecycle: every runtime these tests made is released", log);
        Check(WmvMountedScene.LiveMounts == mountsAtStart, "mount lifecycle: ... and no mount runtime is counted alive", log);
    }

    // ---------------------------------------------------------------- a ridden mount's animation

    /// <summary>
    /// A RIDDEN MOUNT'S ANIMATION PUSHES ON THE WIRE, parsed exactly as the IPC client parses them: a selection without a
    /// role is about the model it names (role "", load 0) and one with a role carries it and the load; a state without a
    /// rider says so, and a state with one keeps the mount in its top level and hands the rider back as a state of its own
    /// with the message's explicitState, arrival time and load. The nested object counts only with hasRider.
    /// </summary>
    static void MountAnimationJsonTests(Action<string> log)
    {
        WmvIpcClient.AnimationSelection a;
        bool ok = WmvIpcClient.ParseAnimationSelection("{\"type\":\"modelAnimation\",\"fileDataID\":1521037,\"sequenceIndex\":2," +
                                                       "\"animID\":0,\"durationMs\":2000,\"loop\":true}", out a);
        Check(ok && a.fileDataID == 1521037 && a.sequenceIndex == 2 && a.durationMs == 2000 && a.role == "" && a.load == 0,
              "mount animation json: a selection without a role parses with role \"\" and load 0", log);
        ok = WmvIpcClient.ParseAnimationSelection("{\"type\":\"modelAnimation\",\"fileDataID\":126407,\"sequenceIndex\":1,\"animID\":0," +
                                                  "\"durationMs\":4000,\"loop\":true,\"role\":\"mount\",\"load\":12}", out a);
        Check(ok && a.role == WmvSlotAnimation.RoleMount && a.load == 12 && a.fileDataID == 126407 && a.sequenceIndex == 1,
              "mount animation json: a selection with role \"mount\" carries its role, load, file and sequence", log);
        Check(!WmvIpcClient.ParseAnimationSelection("{\"type\":\"modelAnimationState\",\"fileDataID\":1}", out a),
              "mount animation json: a line of another type is not taken for a selection", log);

        const string top = "{\"type\":\"modelAnimationState\",\"fileDataID\":126407,\"sequenceIndex\":1,\"playing\":true," +
                           "\"timeMs\":1840,\"speed\":1.0,\"loop\":true,\"explicitState\":true";
        WmvIpcClient.AnimationState s;
        ok = WmvIpcClient.ParseAnimationState(top + "}", out s);
        Check(ok && !s.hasRider && s.load == 0 && s.fileDataID == 126407 && s.timeMs == 1840 && s.playing,
              "mount animation json: a state without a rider has hasRider false and load 0", log);
        ok = WmvIpcClient.ParseAnimationState(top + ",\"load\":12,\"hasRider\":true,\"rider\":{\"sequenceIndex\":145," +
                                              "\"playing\":false,\"timeMs\":730,\"speed\":0.5,\"loop\":true}}", out s);
        Check(ok && s.hasRider && s.load == 12 && s.fileDataID == 126407 && s.sequenceIndex == 1 && s.playing && s.timeMs == 1840 &&
              s.explicitState, "mount animation json: a ridden state keeps the mount in its top level, with hasRider and load", log);
        WmvIpcClient.AnimationState r = s.RiderState();
        Check(r.sequenceIndex == 145 && !r.playing && r.timeMs == 730 && Near(r.speed, 0.5f) && r.loop && r.explicitState &&
              r.load == 12 && r.fileDataID == 0 && r.receivedSeconds == s.receivedSeconds && !r.hasRider,
              "mount animation json: ... and its rider as a state of its own (sequence 145, paused, 730 ms, 0.5x), with the " +
              "message's explicitState, arrival and load", log);
        ok = WmvIpcClient.ParseAnimationState(top + ",\"load\":12,\"rider\":{\"sequenceIndex\":145,\"playing\":true," +
                                              "\"timeMs\":730,\"speed\":1.0,\"loop\":true}}", out s);
        Check(ok && !s.hasRider, "mount animation json: a rider object without hasRider is not taken for a rider", log);
    }

    /// <summary>
    /// WHICH MODEL A PUSH IS ABOUT (WmvSlotAnimation.RouteSelection), on every case WmvMain meets: no role is the model the
    /// push names, unless it names the character riding on screen (the host has dismounted it); role "rider" is the
    /// character and role "mount" its mount, picked by the role alone when both are built from ONE file; the load serial
    /// decides between the character on screen and the one being loaded; a mount of that file still being prepared wins
    /// over the one on screen; a mount role with no mount of that file, a push for a character that is not here and an
    /// unknown role are ignored, with a reason.
    /// </summary>
    static void MountRoutingTests(Action<string> log)
    {
        const int rider = 1011653, mount = 126407, gryphon = 124298, shared = 535052;
        string why;
        var plain = new WmvSlotAnimation.Holding { RiderLoad = 7, RiderFileDataID = rider };
        var riding = new WmvSlotAnimation.Holding { RiderLoad = 7, RiderFileDataID = rider, RiderMounted = true, MountFileDataID = mount };

        Check(WmvSlotAnimation.RouteSelection("", 0, rider, plain, out why) == WmvSlotAnimation.Route.AsBefore,
              "animation route: no role, character not riding -- about the model it names, as before", log);
        Check(WmvSlotAnimation.RouteSelection("", 0, 99, new WmvSlotAnimation.Holding(), out why) == WmvSlotAnimation.Route.AsBefore,
              "animation route: no role, nothing held -- as before", log);
        Check(WmvSlotAnimation.RouteSelection("", 0, rider, riding, out why) == WmvSlotAnimation.Route.HeldForDismount,
              "animation route: no role for the character riding on screen -- held for the dismount", log);
        Check(WmvSlotAnimation.RouteSelection("", 0, mount, riding, out why) == WmvSlotAnimation.Route.AsBefore,
              "animation route: no role for another file while riding -- as before", log);
        var ridingWhileLoading = riding;
        ridingWhileLoading.Loading = true;
        ridingWhileLoading.LoadIsCharacter = true;
        ridingWhileLoading.LoadSerial = 8;
        Check(WmvSlotAnimation.RouteSelection("", 0, rider, ridingWhileLoading, out why) == WmvSlotAnimation.Route.AsBefore,
              "animation route: no role while a load is in flight -- as before, never held", log);

        Check(WmvSlotAnimation.RouteSelection("rider", 7, rider, riding, out why) == WmvSlotAnimation.Route.Rider,
              "animation route: role rider, the load on screen -- the character", log);
        Check(WmvSlotAnimation.RouteSelection("mount", 7, mount, riding, out why) == WmvSlotAnimation.Route.Mount,
              "animation route: role mount, the load on screen and the mount's file -- the mount", log);
        Check(WmvSlotAnimation.RouteSelection("rider", 6, rider, riding, out why) == WmvSlotAnimation.Route.Ignored && why.Length > 0,
              "animation route: role rider for another load -- ignored (" + why + ")", log);
        Check(WmvSlotAnimation.RouteSelection("rider", 7, mount, riding, out why) == WmvSlotAnimation.Route.Ignored,
              "animation route: role rider naming a file other than the character's -- ignored (" + why + ")", log);
        Check(WmvSlotAnimation.RouteSelection("mount", 7, gryphon, riding, out why) == WmvSlotAnimation.Route.Ignored,
              "animation route: role mount naming a file the character does not ride -- ignored (" + why + ")", log);
        Check(WmvSlotAnimation.RouteSelection("mount", 7, mount, plain, out why) == WmvSlotAnimation.Route.Ignored &&
              why == "the character rides no mount", "animation route: role mount while the character rides nothing -- ignored (" + why + ")", log);
        Check(WmvSlotAnimation.RouteSelection("mount", 7, mount, new WmvSlotAnimation.Holding(), out why) == WmvSlotAnimation.Route.Ignored,
              "animation route: role mount with no character on screen -- ignored (" + why + ")", log);
        Check(WmvSlotAnimation.RouteSelection("passenger", 7, mount, riding, out why) == WmvSlotAnimation.Route.Ignored,
              "animation route: an unknown role -- ignored (" + why + ")", log);

        // One file for both models: the role alone decides, and a push without one is the character's.
        var sharedFile = new WmvSlotAnimation.Holding { RiderLoad = 7, RiderFileDataID = shared, RiderMounted = true, MountFileDataID = shared };
        Check(WmvSlotAnimation.RouteSelection("mount", 7, shared, sharedFile, out why) == WmvSlotAnimation.Route.Mount &&
              WmvSlotAnimation.RouteSelection("rider", 7, shared, sharedFile, out why) == WmvSlotAnimation.Route.Rider,
              "animation route: rider and mount built from ONE file -- role mount is the mount, role rider the character", log);
        WmvSlotAnimation.Route sharedMount, sharedRider;
        WmvSlotAnimation.RouteRiddenState(7, shared, sharedFile, out sharedMount, out sharedRider);
        Check(sharedMount == WmvSlotAnimation.Route.Mount && sharedRider == WmvSlotAnimation.Route.Rider,
              "animation route: ... and a ridden state about that file puts its top level on the mount and its rider on the character", log);

        // A mount being prepared (the host replaced the one on screen already) wins over the one on screen with its file.
        var swapping = riding;
        swapping.MountPreparing = gryphon;
        Check(WmvSlotAnimation.RouteSelection("mount", 7, gryphon, swapping, out why) == WmvSlotAnimation.Route.MountPreparing,
              "animation route: role mount naming the mount being prepared -- kept for it", log);
        var sameFileSwap = riding;
        sameFileSwap.MountPreparing = mount;
        Check(WmvSlotAnimation.RouteSelection("mount", 7, mount, sameFileSwap, out why) == WmvSlotAnimation.Route.MountPreparing,
              "animation route: ... also when it has the file of the mount it replaces", log);
        WmvSlotAnimation.Route m, r;
        WmvSlotAnimation.RouteRiddenState(7, gryphon, swapping, out m, out r);
        Check(m == WmvSlotAnimation.Route.MountPreparing && r == WmvSlotAnimation.Route.Rider,
              "animation route: a ridden state for the mount being prepared -- its rider is still the character on screen", log);
        WmvSlotAnimation.RouteRiddenState(7, mount, riding, out m, out r);
        Check(m == WmvSlotAnimation.Route.Mount && r == WmvSlotAnimation.Route.Rider,
              "animation route: a ridden state about the mount on screen -- both halves on screen", log);
        WmvSlotAnimation.RouteRiddenState(6, mount, riding, out m, out r);
        Check(m == WmvSlotAnimation.Route.Ignored && r == WmvSlotAnimation.Route.Ignored,
              "animation route: a ridden state for another load -- neither half", log);

        // A character being loaded while it rides (a reconnect), beside the one on screen.
        var reloading = riding;
        reloading.Loading = true;
        reloading.LoadIsCharacter = true;
        reloading.LoadSerial = 8;
        Check(WmvSlotAnimation.RouteSelection("rider", 8, rider, reloading, out why) == WmvSlotAnimation.Route.RiderOfLoad,
              "animation route: role rider for the load in flight -- the character being loaded", log);
        Check(WmvSlotAnimation.RouteSelection("mount", 8, mount, reloading, out why) == WmvSlotAnimation.Route.Ignored,
              "animation route: role mount for the load before its scene brought a mount -- ignored (" + why + ")", log);
        reloading.LoadMountPreparing = mount;
        Check(WmvSlotAnimation.RouteSelection("mount", 8, mount, reloading, out why) == WmvSlotAnimation.Route.MountPreparing,
              "animation route: role mount for the load's mount being prepared -- kept for it", log);
        Check(WmvSlotAnimation.RouteSelection("rider", 7, rider, reloading, out why) == WmvSlotAnimation.Route.Ignored &&
              WmvSlotAnimation.RouteSelection("mount", 7, mount, reloading, out why) == WmvSlotAnimation.Route.Ignored,
              "animation route: a push for the character on screen while another load is in flight -- ignored (" + why + ")", log);
        WmvSlotAnimation.RouteRiddenState(8, mount, reloading, out m, out r);
        Check(m == WmvSlotAnimation.Route.MountPreparing && r == WmvSlotAnimation.Route.RiderOfLoad,
              "animation route: a ridden state for the load -- the rider waits for the load, the mount is kept for its mount", log);
        var otherLoad = riding;
        otherLoad.Loading = true;
        Check(WmvSlotAnimation.RouteSelection("rider", 0, rider, otherLoad, out why) == WmvSlotAnimation.Route.Ignored,
              "animation route: a role while a model or world model that is no character loads -- ignored (" + why + ")", log);
    }

    /// <summary>Answer every .anim fetch a slot waits on from the files given, as WmvMain's asset routing hands an answer
    /// to WmvSlotAnimation.OnAnimFileBytes. Returns how many were answered.</summary>
    static int DeliverAnimFiles(WmvSlotAnimation anim, WmvModelSlot slot, MountAssets assets)
    {
        int answered = 0;
        foreach (var kv in new List<KeyValuePair<string, int>>(slot.PendingAnimFetch))
        {
            int fdid = 0;
            foreach (var q in assets.Requests)
                if (q.Key == kv.Key) fdid = q.Value;
            byte[] data;
            bool ok = assets.Files.TryGetValue(fdid, out data);
            anim.OnAnimFileBytes(slot, new WmvIpcClient.AssetResponse
            {
                requestId = kv.Key, ok = ok, fileDataID = fdid, data = ok ? data : null, error = ok ? null : "not served",
            }, kv.Value);
            answered++;
        }
        return answered;
    }

    static bool ClockIs(WmvM2Animator a, int sequence, double timeMs, bool playing, float speed)
    {
        return a != null && a.SequenceIndex == sequence && Math.Abs(a.TimeMs - timeMs) < 0.5 && a.IsPlaying == playing &&
               Near(a.Speed, speed);
    }

    /// <summary>
    /// THE TWO CLOCKS OF A MOUNTED CHARACTER, through WmvSlotAnimation -- the code the player runs -- on two slots built from
    /// ONE synthetic file, as a mount and a rider can be: one ridden state applied in one pass puts each half on its own
    /// animator; a frame advances each by its own speed and pause; switching the mount to a sequence whose keys are in a
    /// .anim fetches into the mount's slot and leaves the character's sequence, clock, caches and fetches alone, and the
    /// reverse; a cached switch asks for nothing; StartClock starts a model that just went on from the app's state only
    /// when that state is about what plays and not older than the host's restart; the mounted scene reports what it is
    /// preparing and when it was first described; and -wmvAnimTime poses the mount, then the character under its bone.
    /// </summary>
    static void MountClockTests(Action<string> log)
    {
        if (WmvModelBuilder.Debug_.NoAnim)
        {
            log("lifecycle-test SKIP: mount clocks: -wmvNoAnim builds no animator and switches nothing");
            return;
        }
        const int file = 473370;
        int animFile = M2Synthetic.SkelAnimFileIdBase;             // the fixture's .anim, for its external sequence 1
        int external = M2Synthetic.SkelExternalSequence;
        var assets = new MountAssets();
        assets.Files[animFile] = M2Synthetic.SkeletonAnimFile(0, true);
        byte[] m2 = M2Synthetic.InFileSkeletonModel(file);
        M2ParsedSkin skin = M2SkinParser.Parse(M2Synthetic.TransformSwitchSkin());
        int liveAtStart = WmvRuntimeModel.Live;
        M2ParsedModel mountModel = M2Parser.Parse(m2, 0), riderModel = M2Parser.Parse(m2, 0);
        WmvRuntimeModel mountRt = WmvModelBuilder.Build(mountModel, skin, new Dictionary<int, BlpImage>(), "ClockMount", null);
        WmvRuntimeModel riderRt = WmvModelBuilder.Build(riderModel, skin, new Dictionary<int, BlpImage>(), "ClockRider", null);
        bool built = mountRt != null && riderRt != null && mountRt.Animator != null && riderRt.Animator != null;
        Check(built, "mount clocks: a mount and a rider built from one file, each with its animator", log);
        if (!built)
        {
            if (mountRt != null) mountRt.Dispose();
            if (riderRt != null) riderRt.Dispose();
            return;
        }
        var mount = new WmvModelSlot { Runtime = mountRt, Model = mountModel, M2Bytes = m2, FileDataID = file, SelectedSequence = 0 };
        var rider = new WmvModelSlot { Runtime = riderRt, Model = riderModel, M2Bytes = m2, FileDataID = file, SelectedSequence = 0 };
        var statusLines = new List<string>();
        var anim = new WmvSlotAnimation(assets.Request, s => statusLines.Add(s));
        WmvM2Animator ma = mountRt.Animator, ra = riderRt.Animator;
        double globalWas = WmvM2Animator.GlobalTimeMs;

        // ---- one ridden state, both halves in one pass ----
        var state = new WmvIpcClient.AnimationState
        {
            fileDataID = file, sequenceIndex = 0, playing = true, timeMs = 300, speed = 1f, loop = true, explicitState = true,
            receivedSeconds = WmvIpcClient.NowSeconds, load = 7, hasRider = true,
            rider = new WmvIpcClient.RiderPlayback { sequenceIndex = 0, playing = false, timeMs = 700, speed = 0.5f, loop = true },
        };
        anim.ApplyRidden(mount, rider, state);
        Check(ClockIs(ma, 0, 300, true, 1f), "mount clocks: one ridden state -- the mount takes the top level (300 ms, playing, 1x)", log);
        Check(ClockIs(ra, 0, 700, false, 0.5f), "mount clocks: ... and the character the nested rider, in the same pass (700 ms, paused, 0.5x)", log);
        Check(mount.HaveAppState && mount.LastAppState.timeMs == 300 && rider.HaveAppState && rider.LastAppState.timeMs == 700 &&
              !rider.LastAppState.hasRider, "mount clocks: ... each slot keeps its own half as its app state", log);
        var riderOnly = state;
        riderOnly.timeMs = 900;
        riderOnly.rider.timeMs = 650;
        anim.ApplyRidden(null, rider, riderOnly);
        Check(ClockIs(ma, 0, 300, true, 1f) && ClockIs(ra, 0, 650, false, 0.5f),
              "mount clocks: a ridden state whose mount is not on screen moves the character only", log);

        // ---- a frame: each clock by its own speed and pause ----
        const BindingFlags instance = BindingFlags.Instance | BindingFlags.NonPublic;
        const BindingFlags statics = BindingFlags.Static | BindingFlags.NonPublic;
        MethodInfo lateUpdate = typeof(WmvM2Animator).GetMethod("LateUpdate", instance);
        FieldInfo lastRealtime = typeof(WmvM2Animator).GetField("lastRealtime", instance);
        FieldInfo advancedFrame = typeof(WmvM2Animator).GetField("globalAdvancedFrame", statics);
        FieldInfo lastGlobalRealtime = typeof(WmvM2Animator).GetField("lastGlobalRealtime", statics);
        if (WmvModelBuilder.Debug_.AnimTime >= 0f)
            log("lifecycle-test SKIP: mount clocks: -wmvAnimTime pins every clock, so a frame advances neither");
        else if (lateUpdate == null || lastRealtime == null || advancedFrame == null || lastGlobalRealtime == null)
            Check(false, "mount clocks: LateUpdate and the clock fields are reachable by reflection", log);
        else
        {
            object stampWas = advancedFrame.GetValue(null), readingWas = lastGlobalRealtime.GetValue(null);
            try
            {
                double wait = 2.0 - Time.realtimeSinceStartupAsDouble;      // a reading 0.2 s back must exist (see GlobalClockTests)
                if (wait > 0.0)
                    System.Threading.Thread.Sleep((int)(wait * 1000.0) + 1);
                ra.SetTransportOnly(true, 0.5f);
                double mountBefore = ma.TimeMs, riderBefore = ra.TimeMs;
                advancedFrame.SetValue(null, Time.frameCount - 1);
                lastGlobalRealtime.SetValue(null, Time.realtimeSinceStartupAsDouble - 0.2);
                lastRealtime.SetValue(ma, Time.realtimeSinceStartupAsDouble - 0.2);
                lateUpdate.Invoke(ma, null);
                lastRealtime.SetValue(ra, Time.realtimeSinceStartupAsDouble - 0.2);
                lateUpdate.Invoke(ra, null);
                double mountStep = ma.TimeMs - mountBefore, riderStep = ra.TimeMs - riderBefore;
                Check(mountStep >= 199.0 && mountStep < 260.0 && riderStep >= 99.5 && riderStep < 130.0,
                      "mount clocks: a frame of 200 ms advances the mount by its 1x (" + mountStep.ToString("F1") + " ms) and the " +
                      "character by its own 0.5x (" + riderStep.ToString("F1") + " ms), neither reading the other", log);
                ma.SetTransportOnly(false, 1f);
                mountBefore = ma.TimeMs;
                riderBefore = ra.TimeMs;
                advancedFrame.SetValue(null, Time.frameCount - 1);
                lastRealtime.SetValue(ma, Time.realtimeSinceStartupAsDouble - 0.2);
                lateUpdate.Invoke(ma, null);
                lastRealtime.SetValue(ra, Time.realtimeSinceStartupAsDouble - 0.2);
                lateUpdate.Invoke(ra, null);
                Check(ma.TimeMs == mountBefore && ra.TimeMs - riderBefore >= 99.5,
                      "mount clocks: the mount paused holds while the character's clock, told to play, still runs", log);
            }
            finally
            {
                advancedFrame.SetValue(null, stampWas);
                lastGlobalRealtime.SetValue(null, readingWas);
                WmvM2Animator.GlobalTimeMs = globalWas;
            }
        }

        // ---- sequence switches: one model's never resets the other ----
        anim.ApplyRidden(mount, rider, state);                  // back to 300 ms playing / 700 ms paused at 0.5x
        int asked = assets.Requests.Count;
        anim.SelectSequence(mount, external, false);
        Check(assets.Requests.Count == asked + 1 && assets.Requests[asked].Value == animFile && mount.PendingAnimFetch.Count == 1 &&
              rider.PendingAnimFetch.Count == 0, "mount clocks: the mount switched to a sequence keyed in a .anim fetches it into the mount's slot", log);
        Check(ClockIs(ma, 0, 300, true, 1f) && ClockIs(ra, 0, 700, false, 0.5f),
              "mount clocks: ... both models keep what they play while it is on its way", log);
        Check(DeliverAnimFiles(anim, mount, assets) == 1 && ma.SequenceIndex == external && mountModel.AnimatedSequence == external &&
              mount.AnimFileCache.ContainsKey(animFile) && mount.BoneTrackCache.ContainsKey(external),
              "mount clocks: the .anim arrives -- the mount plays its sequence " + external + ", cached in the mount's slot", log);
        Check(ClockIs(ma, external, 0, true, 1f),
              "mount clocks: ... from its first frame at the app's play/pause and speed (the state named another sequence)", log);
        Check(ClockIs(ra, 0, 700, false, 0.5f) && riderModel.AnimatedSequence == 0 && !rider.AnimFileCache.ContainsKey(animFile) &&
              !rider.BoneTrackCache.ContainsKey(external) && rider.PendingAnimFetch.Count == 0,
              "mount clocks: ... and the character's sequence, clock, caches and fetches are untouched", log);

        WmvIpcClient.AnimationState mountOnExternal = state;
        mountOnExternal.sequenceIndex = external;
        mountOnExternal.timeMs = 250;
        mountOnExternal.rider.timeMs = 710;
        anim.ApplyRidden(mount, rider, mountOnExternal);
        Check(ClockIs(ma, external, 250, true, 1f) && ClockIs(ra, 0, 710, false, 0.5f),
              "mount clocks: the next ridden state puts each model at its own position again", log);

        asked = assets.Requests.Count;
        anim.SelectSequence(rider, external, false);
        Check(assets.Requests.Count == asked + 1 && rider.PendingAnimFetch.Count == 1 && mount.PendingAnimFetch.Count == 0,
              "mount clocks: the character switched to that sequence fetches the .anim into its own slot, not the mount's cache", log);
        Check(DeliverAnimFiles(anim, rider, assets) == 1 && ra.SequenceIndex == external && riderModel.AnimatedSequence == external,
              "mount clocks: ... the character plays it when it arrives", log);
        Check(ClockIs(ma, external, 250, true, 1f) && mountModel.AnimatedSequence == external,
              "mount clocks: ... and the mount's sequence and clock are untouched by the character's switch", log);

        anim.SelectSequence(mount, 0, false);
        Check(ma.SequenceIndex == 0 && ra.SequenceIndex == external && mount.BoneTrackCache.ContainsKey(0),
              "mount clocks: the mount back to its in-file sequence 0 -- read and cached in its slot, the character stays on " + external, log);
        asked = assets.Requests.Count;
        anim.SelectSequence(mount, external, false);
        Check(assets.Requests.Count == asked && ma.SequenceIndex == external && ra.SequenceIndex == external,
              "mount clocks: ... and to " + external + " again from the mount's cache, with nothing asked for", log);
        int statusBefore = statusLines.Count;
        anim.SelectSequence(mount, external, false);
        Check(statusLines.Count == statusBefore + 1 && statusLines[statusBefore] == "Animation unchanged" && ClockIs(ma, external, ma.TimeMs, true, 1f),
              "mount clocks: a selection of what the mount already plays touches nothing", log);

        // ---- the clock of a model that has just gone on ----
        var fresh = new WmvModelSlot { Runtime = riderRt, Model = riderModel, M2Bytes = m2, FileDataID = file };
        float at = WmvSlotAnimation.StartClock(fresh, 0.0, false, 0.75f);
        Check(at == 0f && ClockIs(ra, external, 0, false, 0.75f), "start clock: no app state -- the first frame at the fallback's pause and speed", log);
        // The IPC clock starts when it is first read, and an arrival at or below zero means "not known" to the projection:
        // wait until a quarter of a second ago is a real reading.
        double clockWait = 0.5 - WmvIpcClient.NowSeconds;
        if (clockWait > 0.0)
            System.Threading.Thread.Sleep((int)(clockWait * 1000.0) + 1);
        double arrived = WmvIpcClient.NowSeconds - 0.25;
        fresh.LastAppState = new WmvIpcClient.AnimationState { sequenceIndex = external, playing = true, timeMs = 100, speed = 1f, receivedSeconds = arrived };
        fresh.HaveAppState = true;
        at = WmvSlotAnimation.StartClock(fresh, arrived - 1.0, false, 0.75f);
        Check(at >= 349f && at < 450f && ra.IsPlaying && Near(ra.Speed, 1f) && Math.Abs(ra.TimeMs - at) < 0.5,
              "start clock: a state about what plays, newer than the restart -- its position projected to now (" + at.ToString("F0") + " ms)", log);
        at = WmvSlotAnimation.StartClock(fresh, arrived + 1.0, false, 0.75f);
        Check(at == 0f && ClockIs(ra, external, 0, true, 1f),
              "start clock: the same state older than the restart -- the first frame, with its play/pause and speed", log);
        fresh.LastAppState.sequenceIndex = 0;
        fresh.LastAppState.playing = false;
        at = WmvSlotAnimation.StartClock(fresh, 0.0, true, 0.75f);
        Check(at == 0f && ClockIs(ra, external, 0, false, 1f),
              "start clock: a state about another sequence -- the first frame, with that state's play/pause and speed", log);
        fresh.LastAppState.speed = 0.5f;
        at = WmvSlotAnimation.StartInStep(fresh, 600.0, true, 2f);
        Check(Near(at, 300f) && ClockIs(ra, external, 300, true, 0.5f),
              "start in step: 600 ms of the other model's playback -- this one 300 ms in at its own 0.5x, playing as given", log);
        fresh.HaveAppState = false;
        at = WmvSlotAnimation.StartInStep(fresh, 150.0, false, 2f);
        Check(Near(at, 300f) && ClockIs(ra, external, 300, false, 2f),
              "start in step: no state of its own -- the fallback speed (2x: 300 ms), paused as given", log);
        at = WmvSlotAnimation.StartInStep(fresh, -40.0, true, 1f);
        Check(at == 0f && ClockIs(ra, external, 0, true, 1f), "start in step: no time run -- its first frame", log);

        // ---- what the mounted scene prepares, and when it was first described ----
        var scene = new WmvMountedScene(assets.Request, null, null);
        Check(scene.PreparingFileDataID == 0 && scene.PreparingDescribedAt == double.MaxValue,
              "mount clocks: a mounted scene with nothing to prepare names no file and no time", log);
        var described = new WmvIpcClient.SceneMount { key = "M9", fileDataID = 780001, sequenceIndex = 0, riderSequenceIndex = 0, bone = -1 };
        scene.Retarget(described, 12.5);
        scene.Retarget(new WmvIpcClient.SceneMount { key = "M9", fileDataID = 780001, sequenceIndex = 0, riderSequenceIndex = 0, bone = -1 }, 99.0);
        Check(scene.PreparingFileDataID == 780001 && scene.PreparingDescribedAt == 12.5,
              "mount clocks: a mount being prepared names its file and when its key was FIRST described (a later description keeps it)", log);
        scene.Retarget(new WmvIpcClient.SceneMount { key = "M10", fileDataID = 780002, sequenceIndex = 0, riderSequenceIndex = 0, bone = -1 }, 40.0);
        Check(scene.PreparingFileDataID == 780002 && scene.PreparingDescribedAt == 40.0,
              "mount clocks: a new key is described anew", log);
        scene.CancelTarget();
        Check(scene.PreparingFileDataID == 0 && scene.PreparingDescribedAt == double.MaxValue,
              "mount clocks: ... and nothing is prepared once the target is dropped", log);
        scene.Dispose();

        // ---- -wmvAnimTime: the mount posed, then the character under its bone ----
        M2AttachmentDef att;
        if (M2Parser.AttachmentFor(mountModel, 11, out att) && att.Bone == 2)
        {
            anim.SelectSequence(mount, 0, false);
            anim.SelectSequence(rider, 0, false);
            Transform body = riderRt.Root.transform;
            Vector3 offset = WmvCharacterDresser.AttachmentLocalPosition(att.Position, mountModel.Bones[att.Bone].Pivot);
            body.SetParent(mountRt.Bones[att.Bone], false);
            body.localPosition = offset;
            try
            {
                ma.ApplyPose(0f);
                ra.ApplyPose(0f);
                WmvSlotAnimation.PoseMountedAt(mountRt, riderRt, 250f);
                // Sequence 0 moves bone i by (i + 1, 0, 0) at 500 ms, so by half of that at 250 ms.
                Vector3 halfway0 = mountRt.BoneRestPositions[0] + UnityPosition(new WowVec3(0.5f, 0f, 0f));
                bool bothAt = NearV(mountRt.Bones[0].localPosition, halfway0, 1e-4f) && NearV(riderRt.Bones[0].localPosition, halfway0, 1e-4f);
                Check(bothAt && WmvM2Animator.GlobalTimeMs == 250.0,
                      "pinned pose: both models at the instant (bone 0 halfway through its 500 ms key), the global clock with them", log);
                Check(NearV(body.position, mountRt.Bones[att.Bone].localToWorldMatrix.MultiplyPoint3x4(offset), 1e-4f) &&
                      !NearV(mountRt.Bones[att.Bone].position, UnityPosition(mountModel.Bones[att.Bone].Pivot), 1e-3f),
                      "pinned pose: ... the character's root where the mount's posed bone carries it, off the bone's rest", log);
                WmvSlotAnimation.PoseMountedAt(null, riderRt, 0f);
                Check(NearV(riderRt.Bones[0].localPosition, mountRt.BoneRestPositions[0], 1e-4f) &&
                      NearV(mountRt.Bones[0].localPosition, halfway0, 1e-4f),
                      "pinned pose: with no mount, only the character is posed", log);
            }
            finally
            {
                body.SetParent(null, false);
                WmvM2Animator.GlobalTimeMs = globalWas;
            }
        }
        else
            Check(false, "pinned pose: the fixture's attachment 11 is on bone 2", log);

        mountRt.Dispose();
        riderRt.Dispose();
        Check(WmvRuntimeModel.Live == liveAtStart, "mount clocks: every runtime these tests made is released", log);
    }

    // ---------------------------------------------------------------- what the camera frames on a mount

    /// <summary>A box's eight corners carried through a matrix one by one, grown into min/max: what the framing must
    /// equal, worked out without it.</summary>
    static void CornersByHand(Bounds local, Matrix4x4 toWorld, ref bool any, ref Vector3 min, ref Vector3 max)
    {
        for (int x = -1; x <= 1; x += 2)
            for (int y = -1; y <= 1; y += 2)
                for (int z = -1; z <= 1; z += 2)
                {
                    Vector3 p = toWorld.MultiplyPoint3x4(local.center + new Vector3(x * local.extents.x, y * local.extents.y,
                                                                                    z * local.extents.z));
                    if (!any) { min = p; max = p; any = true; }
                    else { min = Vector3.Min(min, p); max = Vector3.Max(max, p); }
                }
    }

    static bool SameBox(Bounds b, Vector3 min, Vector3 max, float eps)
    {
        return NearV(b.min, min, eps) && NearV(b.max, max, eps);
    }

    /// <summary>
    /// WHAT THE CAMERA FRAMES WHILE A CHARACTER RIDES (WmvMountedScene.UnionBounds): a box in a body root's space carried
    /// into the world corner by corner, under a parent that moves, turns and scales it; a skinned character hung from the
    /// turned, moved bone of an animated mount, framed with the mount as one box -- the corners of both carried by hand,
    /// never the two boxes joined in their own spaces -- measured again at another instant of the mount's clock and under a
    /// moved mount root; the character alone once it is off; and the same through a mounted scene's commit and dismount.
    /// Carrying the boxes allocates nothing, where the runtime counts allocations per thread.
    /// </summary>
    static void MountFramingTests(Action<string> log)
    {
        // ---- the arithmetic, on a plain hierarchy ----
        var parentGo = new GameObject("MountFramingParent");
        var childGo = new GameObject("MountFramingChild");
        Transform parent = parentGo.transform, child = childGo.transform;
        parent.position = new Vector3(3f, -2f, 5f);
        parent.rotation = Quaternion.Euler(20f, 45f, -10f);
        child.SetParent(parent, false);
        child.localPosition = new Vector3(0.5f, 1f, -0.25f);
        child.localRotation = Quaternion.Euler(0f, 30f, 0f);
        child.localScale = new Vector3(1.5f, 1.5f, 1.5f);
        var box = new Bounds(new Vector3(0.1f, 0.9f, 0f), new Vector3(0.8f, 1.8f, 0.4f));
        bool any = false;
        Vector3 min = Vector3.zero, max = Vector3.zero;
        CornersByHand(box, child.localToWorldMatrix, ref any, ref min, ref max);
        Bounds world = WmvMountedScene.WorldBounds(box, child.localToWorldMatrix);
        Check(SameBox(world, min, max, 1e-4f),
              "mount framing: a box under a moved, turned and scaled parent is the box around its eight corners carried by hand", log);
        Check(!NearV(world.center, box.center, 0.5f) && world.size.y > box.size.y * 1.2f,
              "mount framing: ... not the box in its own space: the parent's move, turn and scale are all applied", log);
        Check(SameBox(WmvMountedScene.WorldBounds(box, Matrix4x4.identity), box.min, box.max, 1e-5f),
              "mount framing: through the identity the box is unchanged", log);
        Bounds quarter = WmvMountedScene.WorldBounds(box, Matrix4x4.TRS(Vector3.zero, Quaternion.Euler(0f, 90f, 0f), Vector3.one));
        Check(Near(quarter.extents.x, box.extents.z) && Near(quarter.extents.z, box.extents.x) && Near(quarter.extents.y, box.extents.y),
              "mount framing: a quarter turn about the vertical swaps the box's width and depth", log);
        UnityEngine.Object.DestroyImmediate(childGo);
        UnityEngine.Object.DestroyImmediate(parentGo);

        if (WmvModelBuilder.Debug_.NoAnim)
        {
            log("lifecycle-test SKIP: mount framing on built models: -wmvNoAnim builds no animator to turn the mount's bone");
            return;
        }

        // ---- a skinned character on the turned, moved bone of an animated mount ----
        int liveAtStart = WmvRuntimeModel.Live;
        byte[] m2 = M2Synthetic.InFileSkeletonModel(473370);
        M2ParsedSkin skin = M2SkinParser.Parse(M2Synthetic.TransformSwitchSkin());
        M2ParsedModel mountModel = M2Parser.Parse(m2, 0), riderModel = M2Parser.Parse(m2, 0);
        WmvRuntimeModel mountRt = WmvModelBuilder.Build(mountModel, skin, new Dictionary<int, BlpImage>(), "FramingMount", null);
        WmvRuntimeModel riderRt = WmvModelBuilder.Build(riderModel, skin, new Dictionary<int, BlpImage>(), "FramingRider", null);
        M2AttachmentDef att;
        bool built = mountRt != null && riderRt != null && mountRt.Animator != null && mountRt.Bones.Length == 3 &&
                     M2Parser.AttachmentFor(mountModel, 11, out att) && att.Bone == 2;
        Check(built, "mount framing: a mount and a character built, the mount's attachment 11 on its leaf bone 2", log);
        if (!built)
        {
            if (mountRt != null) mountRt.Dispose();
            if (riderRt != null) riderRt.Dispose();
            return;
        }
        M2Parser.AttachmentFor(mountModel, 11, out att);
        Transform bone = mountRt.Bones[att.Bone];
        Transform body = riderRt.Root.transform;
        Vector3 seat = WmvCharacterDresser.AttachmentLocalPosition(att.Position, mountModel.Bones[att.Bone].Pivot);
        double globalWas = WmvM2Animator.GlobalTimeMs;
        var mover = new GameObject("MountFramingMover");
        try
        {
            body.SetParent(bone, false);
            body.localPosition = seat;
            body.localRotation = Quaternion.identity;
            body.localScale = new Vector3(1.25f, 1.25f, 1.25f);
            WmvM2Animator.GlobalTimeMs = 375.0;           // bone 1 turns on its global sequence, carrying bone 2
            mountRt.Animator.ApplyPose(375f);
            Check(!SameRotation(bone.rotation, Quaternion.identity) &&
                  !NearV(bone.position, UnityPosition(mountModel.Bones[att.Bone].Pivot), 1e-3f),
                  "mount framing: the mount's bone the character hangs from is turned and moved from its rest", log);

            any = false;
            CornersByHand(mountRt.Bounds, mountRt.Root.transform.localToWorldMatrix, ref any, ref min, ref max);
            CornersByHand(riderRt.Bounds, body.localToWorldMatrix, ref any, ref min, ref max);
            Bounds union = WmvMountedScene.UnionOf(mountRt, riderRt);
            Check(SameBox(union, min, max, 1e-4f),
                  "mount framing: the mount and the character on its bone frame as the box around both models' corners, each carried " +
                  "through its root by hand", log);
            Vector3 naiveMin = Vector3.Min(mountRt.Bounds.min, riderRt.Bounds.min), naiveMax = Vector3.Max(mountRt.Bounds.max, riderRt.Bounds.max);
            Check(!SameBox(union, naiveMin, naiveMax, 1e-3f),
                  "mount framing: ... which is not the two boxes joined in their own spaces", log);

            WmvM2Animator.GlobalTimeMs = 125.0;
            mountRt.Animator.ApplyPose(125f);
            any = false;
            CornersByHand(mountRt.Bounds, mountRt.Root.transform.localToWorldMatrix, ref any, ref min, ref max);
            CornersByHand(riderRt.Bounds, body.localToWorldMatrix, ref any, ref min, ref max);
            Bounds later = WmvMountedScene.UnionOf(mountRt, riderRt);
            Check(SameBox(later, min, max, 1e-4f) && !SameBox(later, union.min, union.max, 1e-3f),
                  "mount framing: at another instant of the mount's clock the box is measured where the bone has carried the character", log);

            mover.transform.position = new Vector3(-4f, 1f, 2f);
            mover.transform.rotation = Quaternion.Euler(0f, 60f, 15f);
            mountRt.Root.transform.SetParent(mover.transform, false);
            any = false;
            CornersByHand(mountRt.Bounds, mountRt.Root.transform.localToWorldMatrix, ref any, ref min, ref max);
            CornersByHand(riderRt.Bounds, body.localToWorldMatrix, ref any, ref min, ref max);
            Bounds moved = WmvMountedScene.UnionOf(mountRt, riderRt);
            Check(SameBox(moved, min, max, 1e-4f) && !NearV(moved.center, later.center, 0.5f),
                  "mount framing: a mount root moved and turned carries the mount's box and the character's with it", log);
            mountRt.Root.transform.SetParent(null, false);

            // No allocation, where the runtime counts allocations per thread (checked first: a count that does not see a
            // 4 KB array cannot see anything). Where it does not, -wmvAllocCheck's frame window is the evidence.
            long calibrate = GC.GetAllocatedBytesForCurrentThread();
            var sample = new byte[4096];
            long counted = GC.GetAllocatedBytesForCurrentThread() - calibrate;
            GC.KeepAlive(sample);
            if (counted < 4096)
                log("lifecycle-test SKIP: mount framing allocation: this runtime does not count allocations per thread (a 4 KB " +
                    "array counted " + counted + " bytes)");
            else
            {
                long before = GC.GetAllocatedBytesForCurrentThread();
                Bounds last = new Bounds();
                for (int i = 0; i < 1000; i++)
                    last = WmvMountedScene.UnionOf(mountRt, riderRt);
                long spent = GC.GetAllocatedBytesForCurrentThread() - before;
                Check(spent == 0 && last.size.x > 0f,
                      "mount framing: a thousand unions of the two models allocate nothing (" + spent + " bytes counted)", log);
            }

            body.SetParent(null, false);
            body.localPosition = Vector3.zero;
            body.localRotation = Quaternion.identity;
            body.localScale = Vector3.one;
            Check(SameBox(WmvMountedScene.WorldBounds(riderRt.Bounds, body.localToWorldMatrix), riderRt.Bounds.min, riderRt.Bounds.max, 1e-5f),
                  "mount framing: off the mount, at the origin with the identity, the character frames as its own box", log);
        }
        finally
        {
            WmvM2Animator.GlobalTimeMs = globalWas;
            body.SetParent(null, false);
            if (mountRt.Root != null) mountRt.Root.transform.SetParent(null, false);
            UnityEngine.Object.DestroyImmediate(mover);
        }

        // ---- through a mounted scene: both models while the character rides, the character alone after ----
        const int mountFile = 780201, skinFile = 780203;
        var assets = new MountAssets();
        assets.Files[mountFile] = M2Synthetic.InFileSkeletonModel(skinFile);
        assets.Files[skinFile] = M2Synthetic.TransformSwitchSkin();
        var scene = new WmvMountedScene(assets.Request, null, null);
        Bounds framedNow;
        Check(!scene.UnionBounds(riderRt, out framedNow) && SameBox(framedNow, riderRt.Bounds.min, riderRt.Bounds.max, 1e-5f),
              "mount framing: a mounted scene with no mount on screen frames the character alone", log);
        WmvIpcClient.SceneMount m = MountOf("M70", mountFile, 2, new float[] { 0.5f, 0f, 1.5f }, 1.25f, 0, 0, 0);
        scene.Retarget(m);
        assets.Deliver(scene);
        bool newMount;
        WmvIpcClient.MountAnswer answer = scene.Commit(m, riderRt, out newMount);
        bool rides = answer.Status == "applied" && scene.Mount.Runtime != null && body.parent == scene.Mount.Runtime.Bones[2];
        Check(rides, "mount framing: a mount committed through a mounted scene, the character on its bone 2", log);
        if (rides)
        {
            scene.Mount.Runtime.Animator.ApplyPose(250f);
            any = false;
            CornersByHand(scene.Mount.Runtime.Bounds, scene.Mount.Runtime.Root.transform.localToWorldMatrix, ref any, ref min, ref max);
            CornersByHand(riderRt.Bounds, body.localToWorldMatrix, ref any, ref min, ref max);
            Check(scene.UnionBounds(riderRt, out framedNow) && SameBox(framedNow, min, max, 1e-4f),
                  "mount framing: ... the scene frames the mount and the character together, as worked out by hand", log);
            Check(!scene.UnionBounds(mountRt, out framedNow),
                  "mount framing: ... and a body that is not the one riding it is framed alone", log);
        }
        scene.Dismount();
        Check(!scene.UnionBounds(riderRt, out framedNow) && SameBox(framedNow, riderRt.Bounds.min, riderRt.Bounds.max, 1e-5f),
              "mount framing: after the dismount the scene frames the character alone again, its own box at the origin", log);
        scene.Dispose();
        mountRt.Dispose();
        riderRt.Dispose();
        Check(WmvRuntimeModel.Live == liveAtStart, "mount framing: every runtime these tests made is released", log);
    }

    public static void RunAll(Action<string> log)
    {
        passed = failed = 0;
        foreach (bool skinned in new[] { false, true })
            foreach (int keyed in new[] { 1, 0 })
                Run(skinned, keyed, log);
        GeosetTests(log);
        SubmeshVisibilityTests(log);
        OutputGateTests(log);
        EmitterTests(log);
        CharacterTests(log);
        NpcEquipmentTests(log);
        MountTests(log);
        ZoomTests(log);
        MapObjectTests(log);
        BackgroundTests(log);
        AssetCacheTests(log);
        StreamReaderTests(log);
        TerrainTests(log);
        TerrainHiddenModelTests(log);
        TerrainLateAnswerMainTests(log);
        log(string.Format("lifecycle-test: {0} passed, {1} failed", passed, failed));
    }

    // ---------------------------------------------------------------- the host's stream (protocol 10)

    /// <summary>A stream that hands out at most Chunk bytes per Read, as a socket may.</summary>
    class ChunkedStream : System.IO.Stream
    {
        readonly byte[] data;
        int at;
        public int Chunk = 1;
        public ChunkedStream(byte[] data) { this.data = data; }
        public override int Read(byte[] buffer, int offset, int count)
        {
            int n = Math.Min(Math.Min(count, Chunk), data.Length - at);
            if (n <= 0) return 0;
            Buffer.BlockCopy(data, at, buffer, offset, n);
            at += n;
            return n;
        }
        public override bool CanRead { get { return true; } }
        public override bool CanSeek { get { return false; } }
        public override bool CanWrite { get { return false; } }
        public override long Length { get { return data.Length; } }
        public override long Position { get { return at; } set { throw new NotSupportedException(); } }
        public override void Flush() { }
        public override long Seek(long o, System.IO.SeekOrigin s) { throw new NotSupportedException(); }
        public override void SetLength(long v) { throw new NotSupportedException(); }
        public override void Write(byte[] b, int o, int c) { throw new NotSupportedException(); }
    }

    static byte[] Bytes(params object[] parts)
    {
        var ms = new System.IO.MemoryStream();
        foreach (object p in parts)
        {
            byte[] b = p as byte[] ?? System.Text.Encoding.UTF8.GetBytes((string)p);
            ms.Write(b, 0, b.Length);
        }
        return ms.ToArray();
    }

    /// <summary>
    /// WmvStreamReader: lines and payloads come out as they went in, whatever chunks the socket hands over and however
    /// small the buffer -- a payload holding newlines, one longer than the buffer, a carriage return before a newline,
    /// a line longer than the buffer, a last line with no newline; a payload not followed by its newline, one cut
    /// short and one too large are refused.
    /// </summary>
    static void StreamReaderTests(Action<string> log)
    {
        var small = new byte[300];
        var large = new byte[5000];
        for (int i = 0; i < small.Length; i++) small[i] = (byte)(i * 7);       // includes '\n' (10) and '\r' (13)
        for (int i = 0; i < large.Length; i++) large[i] = (byte)(i * 13 + 1);
        string longLine = "{\"type\":\"x\",\"data\":\"" + new string('A', 1000) + "\"}";
        byte[] wire = Bytes("{\"type\":\"a\"}\n",
                            "{\"type\":\"assetResponse\",\"payloadBytes\":300}\n", small, "\n",
                            "{\"type\":\"b\"}\r\n",
                            longLine + "\n",
                            "{\"type\":\"characterImage\",\"payloadBytes\":5000}\n", large, "\n",
                            "tail");
        foreach (int chunk in new[] { 1, 7, 64, 1000 })
        {
            var r = new WmvStreamReader(new ChunkedStream(wire) { Chunk = chunk }, 64);
            bool ok = r.ReadLine() == "{\"type\":\"a\"}";
            string h1 = r.ReadLine();
            ok &= WmvStreamReader.PayloadBytesOf(h1) == 300;
            byte[] p1 = r.ReadPayload(300);
            ok &= p1.Length == 300 && System.Linq.Enumerable.SequenceEqual(p1, small);
            ok &= r.ReadLine() == "{\"type\":\"b\"}";
            ok &= r.ReadLine() == longLine;
            string h2 = r.ReadLine();
            byte[] p2 = r.ReadPayload(WmvStreamReader.PayloadBytesOf(h2));
            ok &= p2.Length == 5000 && System.Linq.Enumerable.SequenceEqual(p2, large);
            ok &= r.ReadLine() == "tail" && r.ReadLine() == null;
            Check(ok, "host stream: lines and payloads read back exactly, " + chunk + " byte(s) per socket read, 64-byte buffer", log);
        }

        // As the real player reads: a 1 MB buffer, so a whole frame -- header, payload and its newline -- sits in it;
        // and a payload that ends exactly at the end of a 64-byte buffer, its newline in the next read.
        {
            var r = new WmvStreamReader(new ChunkedStream(wire) { Chunk = wire.Length }, 1 << 20);
            bool ok = r.ReadLine() == "{\"type\":\"a\"}";
            ok &= System.Linq.Enumerable.SequenceEqual(r.ReadPayload(WmvStreamReader.PayloadBytesOf(r.ReadLine())), small);
            ok &= r.ReadLine() == "{\"type\":\"b\"}" && r.ReadLine() == longLine;
            ok &= System.Linq.Enumerable.SequenceEqual(r.ReadPayload(WmvStreamReader.PayloadBytesOf(r.ReadLine())), large);
            ok &= r.ReadLine() == "tail" && r.ReadLine() == null;
            Check(ok, "host stream: whole frames inside the buffer (the real player's 1 MB) read back exactly", log);

            string head = "{\"payloadBytes\":";
            int k = 64 - (head.Length + 4);                     // header "{"payloadBytes":NN}\n" (two digits) + k bytes = 64
            byte[] exact = new byte[k];
            for (int i = 0; i < k; i++) exact[i] = (byte)'\n';
            var e = new WmvStreamReader(new ChunkedStream(Bytes(head + k + "}\n", exact, "\nnext\n")) { Chunk = 64 }, 64);
            bool okExact = System.Linq.Enumerable.SequenceEqual(e.ReadPayload(WmvStreamReader.PayloadBytesOf(e.ReadLine())), exact) &&
                           e.ReadLine() == "next";
            Check(okExact, "host stream: a payload (all newline bytes) ending exactly at the buffer's end, its newline in the next read", log);
        }

        bool threw = false;
        try
        {
            var r = new WmvStreamReader(new ChunkedStream(Bytes("{\"payloadBytes\":4}\n", "abcdX{}\n")) { Chunk = 3 }, 64);
            r.ReadPayload(WmvStreamReader.PayloadBytesOf(r.ReadLine()));
        }
        catch (System.IO.EndOfStreamException) { }
        catch (System.IO.IOException) { threw = true; }
        Check(threw, "host stream: a payload not followed by its newline ends the connection (out of step)", log);

        threw = false;
        try
        {
            var r = new WmvStreamReader(new ChunkedStream(Bytes("{\"payloadBytes\":100}\n", new byte[50])) { Chunk = 16 }, 64);
            r.ReadPayload(WmvStreamReader.PayloadBytesOf(r.ReadLine()));
        }
        catch (System.IO.EndOfStreamException) { threw = true; }
        Check(threw, "host stream: a payload cut short ends the connection", log);

        threw = false;
        try { new WmvStreamReader(new ChunkedStream(new byte[0]), 64).ReadPayload(WmvStreamReader.MaxPayloadBytes + 1); }
        catch (System.IO.EndOfStreamException) { }          // the limit is gone: the payload was allocated and read
        catch (System.IO.IOException) { threw = true; }     // the limit refused it
        Check(threw, "host stream: a payload larger than the limit is refused before anything is allocated", log);

        Check(WmvStreamReader.PayloadBytesOf("{\"type\":\"a\",\"payloadBytes\": 42}") == 42 &&
              WmvStreamReader.PayloadBytesOf("{\"type\":\"a\"}") == 0,
              "host stream: a line's payloadBytes is found without parsing it", log);

        ReadLoopTests(log);
    }

    /// <summary>
    /// The player's read loop itself (WmvIpcClient.NextMessage): payloadBytes taken from the parsed line; a binary
    /// asset's bytes and a binary image's pixels (swizzled to RGBA) made ready; a base64 asset still decoded; a line
    /// whose JSON does not parse skipped together with its payload, so the next message is read whole.
    /// </summary>
    static void ReadLoopTests(Action<string> log)
    {
        var go = new GameObject("ReadLoopTest");
        var ipc = go.AddComponent<WmvIpcClient>();
        byte[] file = { 1, 2, 10, 13, 255 };
        byte[] bgra = { 10, 20, 30, 40, 50, 60, 70, 255 };                 // two pixels, B G R A
        byte[] wire = Bytes(
            "{\"type\":\"assetResponse\",\"requestId\":\"u1\",\"ok\":true,\"fileDataID\":7,\"byteLength\":5,",
            "\"encoding\":\"binary\",\"payloadBytes\":5}\n", file, "\n",
            "{\"type\":\"characterImage\",\"hash\":\"body-1\",\"kind\":\"body\",\"width\":2,\"height\":1,",
            "\"format\":\"bgra8\",\"encoding\":\"binary\",\"payloadBytes\":8}\n", bgra, "\n",
            "{\"type\":\"assetResponse\",\"payloadBytes\":4, this is not JSON\n", new byte[] { 9, 10, 11, 12 }, "\n",
            "{\"type\":\"assetResponse\",\"requestId\":\"u2\",\"ok\":true,\"encoding\":\"base64\",\"data\":\"AQIK\"}\n");
        var r = new WmvStreamReader(new ChunkedStream(wire) { Chunk = 5 }, 32);
        WmvIpcClient.ReadForTest a = ipc.NextMessageForTest(r);
        Check(a != null && a.Type == "assetResponse" && a.Encoding == "binary" && a.PayloadBytes == 5 && a.AssetBytes != null &&
              System.Linq.Enumerable.SequenceEqual(a.AssetBytes, file),
              "read loop: a binary asset's bytes are its payload, its size taken from the parsed line", log);
        WmvIpcClient.ReadForTest img = ipc.NextMessageForTest(r);
        byte[] rgba = img != null && img.Image != null && img.Image.image != null ? img.Image.image.Rgba : null;
        Check(rgba != null && System.Linq.Enumerable.SequenceEqual(rgba, new byte[] { 30, 20, 10, 40, 70, 60, 50, 255 }) &&
              img.Image.image.Width == 2 && img.Image.image.HasAlpha,
              "read loop: a binary image's pixels come out as RGBA, with its alpha seen", log);
        WmvIpcClient.ReadForTest b = ipc.NextMessageForTest(r);
        Check(b != null && b.Type == "assetResponse" && b.Encoding == "base64" && b.AssetBytes != null &&
              System.Linq.Enumerable.SequenceEqual(b.AssetBytes, new byte[] { 1, 2, 10 }),
              "read loop: a line that does not parse is skipped with its payload, and the next (base64) message read whole", log);
        Check(ipc.NextMessageForTest(r) == null, "read loop: the end of the stream ends it", log);
        UnityEngine.Object.DestroyImmediate(go);
    }

    // ---------------------------------------------------------------- the session asset cache (protocol 9)

    /// <summary>
    /// WmvAssetCache on its own: nothing kept without an epoch; a file by FileDataID and by path; a request joining one
    /// already out; what the last three loads used kept and nothing older; the budget; a new epoch empties it and an
    /// answer asked under the old one is not kept.
    /// </summary>
    static void AssetCacheTests(Action<string> log)
    {
        var c = new WmvAssetCache();
        byte[] model = new byte[1000], skin = new byte[200], other = new byte[50];

        WmvAssetCache.Flight early = c.BeginFlight(100, "a.m2", "u1", WmvAssetCache.FileKind.Model, false);
        c.Store(c.EndFlight("u1") ?? early, 100, "a.m2", model);
        Check(!c.Enabled && c.Count == 0 && c.Take(100, null) == null && !c.Join(100, null, "u0"),
              "asset cache: a host that states no epoch gets nothing kept and nothing joined", log);

        c.SetEpoch(1);
        c.BeginGeneration(100);                                // generation 1
        WmvAssetCache.Flight f = c.BeginFlight(0, "a.m2", "u2", WmvAssetCache.FileKind.Other, false);
        Check(c.Join(0, "a.m2", "u3") && !c.Join(100, null, "u4"),
              "asset cache: a request for a file already asked for (by the same key) joins it", log);
        WmvAssetCache.Flight ended = c.EndFlight("u2");
        Check(ended == f && ended.Waiters.Count == 1 && ended.Waiters[0] == "u3" && !c.Join(0, "a.m2", "u5"),
              "asset cache: the answer ends the flight with the request that joined it, and nothing joins it after", log);
        c.Store(ended, 100, "character/a.m2", model);
        WmvAssetCache.Entry byId = c.Take(100, null), byAsked = c.Take(0, "a.m2"), byAnswered = c.Take(0, "character/a.m2");
        Check(byId != null && byAsked == byId && byAnswered == byId && byId.Data == model && c.Hits == 3 && c.Bytes == 1000,
              "asset cache: a kept file is found by its FileDataID, the path asked for and the path answered", log);

        // A load by path that knows its FileDataID joins a prefetch of the same file still out.
        c.BeginFlight(250, null, "u10", WmvAssetCache.FileKind.Model, true);
        Check(c.Join(250, "b.m2", "u11"), "asset cache: a load by path with its FileDataID joins the prefetch of that file", log);
        c.Store(c.EndFlight("u10"), 250, "", other);
        Check(c.Take(0, "b.m2") != null, "asset cache: and the path it named finds the file the prefetch brought", log);

        WmvAssetCache.Flight s = c.BeginFlight(200, null, "u6", WmvAssetCache.FileKind.Other, true);
        c.Store(c.EndFlight("u6"), 200, "", skin);
        c.BeginGeneration(0);                                  // generation 2
        c.Take(100, null);                                     // 100 used again in generation 2; 200 last in 1
        c.BeginGeneration(0);                                  // generation 3: the two loads before it stay
        Check(c.Holds(200) && c.Holds(100), "asset cache: the files of the two loads before stay (A -> B -> A)", log);
        c.BeginGeneration(0);                                  // generation 4: 200 was last used in 1
        Check(!c.Holds(200) && c.Holds(100), "asset cache: a file none of the last three loads used goes", log);
        c.BeginGeneration(0);                                  // generation 5: 100 was last used in 2
        Check(!c.Holds(100) && c.Take(0, "a.m2") == null && c.Take(0, "character/a.m2") == null,
              "asset cache: and with it its paths", log);

        WmvAssetCache.Flight p = c.BeginFlight(300, null, "u7", WmvAssetCache.FileKind.Model, true);
        c.Store(c.EndFlight("u7"), 300, "", other);
        c.BeginGeneration(0);
        c.BeginGeneration(0);
        Check(c.Touch(300) == other, "asset cache: a prefetched file is kept for the loads that follow", log);

        // The budget: the least recently used unprotected file goes first; the model on screen and a prefetched
        // model stay; a file just stored stays even alone over the budget.
        var b = new WmvAssetCache { Budget = 2500 };
        b.SetEpoch(1);
        b.BeginGeneration(10);                                 // model 10 on screen
        b.Store(b.BeginFlight(10, null, "b1", WmvAssetCache.FileKind.Other, false), 10, "", new byte[1000]);
        b.Store(b.BeginFlight(11, null, "b2", WmvAssetCache.FileKind.Other, false), 11, "", new byte[1000]);
        b.Protect(12);                                         // the prefetched partner
        b.Store(b.BeginFlight(12, null, "b3", WmvAssetCache.FileKind.Model, true), 12, "", new byte[1000]);
        Check(b.Holds(10) && !b.Holds(11) && b.Holds(12) && b.Bytes == 2000 && b.Evictions == 1,
              "asset cache: past the budget the least recently used file goes, not the model on screen or the prefetched one", log);
        b.Store(b.BeginFlight(13, null, "b4", WmvAssetCache.FileKind.Other, false), 13, "", new byte[3000]);
        Check(b.Holds(13) && b.Holds(10) && b.Holds(12),
              "asset cache: a file larger than the budget is still kept for the load that asked for it", log);

        WmvAssetCache.Flight old = c.BeginFlight(400, null, "u8", WmvAssetCache.FileKind.Other, false);
        c.SetEpoch(2);
        Check(c.Count == 0 && c.Bytes == 0 && c.Touch(300) == null && !c.Join(400, null, "u9"),
              "asset cache: another client's epoch empties it, and a request out under the old one is not joined", log);
        c.Store(c.EndFlight("u8"), 400, "", other);
        Check(c.Count == 0, "asset cache: an answer to a request asked under the old epoch is not kept", log);

        AssetCacheWiringTests(log);
    }

    /// <summary>
    /// The cache as WmvIpcClient uses it, with no connection (FeedForTest / PumpForTest / SentForTest): the epoch from
    /// loadWoWModel; a request sent once and a second one joining it, both answered; a cached answer handed out by
    /// Update, never inside the request; a prefetch fetching a model, then its skin and skeleton, and not raised to the
    /// player; a load joining a prefetch; a world model's request kept out of it.
    /// </summary>
    static void AssetCacheWiringTests(Action<string> log)
    {
        var go = new GameObject("AssetCacheWiringTest");
        var ipc = go.AddComponent<WmvIpcClient>();
        var sent = new List<string>();
        var answered = new List<WmvIpcClient.AssetResponse>();
        ipc.SentForTest = s => sent.Add(s);
        ipc.OnAssetResponse = r => answered.Add(r);
        byte[] model = M2Synthetic.SkeletonModel(5002, 5001), skin = new byte[64], skel = new byte[32], file = new byte[100];

        ipc.FeedForTest("loadWoWModel", null, false, 900, "x.m2", null, false, 7, null);
        Check(ipc.Cache.Enabled && ipc.Cache.Epoch == 7, "asset cache wiring: loadWoWModel states the epoch", log);

        string a = ipc.RequestAssetByFileDataID(900);
        string b = ipc.RequestAssetByFileDataID(900);
        Check(sent.Count == 1 && a != b, "asset cache wiring: a second request for a file out joins it (one request sent)", log);
        ipc.FeedForTest("assetResponse", a, true, 900, "x.m2", file, true, 0, null);
        Check(answered.Count == 2 && answered[0].requestId == a && answered[1].requestId == b && answered[1].data == file,
              "asset cache wiring: the answer goes to both requests", log);

        string c = ipc.RequestAssetByFileDataID(900);
        bool notInline = answered.Count == 2;
        ipc.PumpForTest();
        Check(notInline && sent.Count == 1 && answered.Count == 3 && answered[2].requestId == c && answered[2].fromCache &&
              answered[2].data == file,
              "asset cache wiring: a kept file is answered by Update, after the request returned, with nothing sent", log);

        string w = ipc.RequestAssetByFileDataID(901, false);
        ipc.FeedForTest("assetResponse", w, true, 901, "", file, true, 0, null);
        string w2 = ipc.RequestAssetByFileDataID(901, false);
        Check(sent.Count == 3 && !ipc.Cache.Holds(901) && w2 != w,
              "asset cache wiring: a request kept out of the cache (a world model's) is neither kept nor answered from it", log);

        int before = answered.Count, sentBefore = sent.Count;
        ipc.FeedForTest("prefetchAssets", null, false, 0, null, null, false, 7, new[] { 5000 });
        Check(sent.Count == sentBefore + 1 && sent[sent.Count - 1].Contains("5000"),
              "asset cache wiring: prefetchAssets asks for the model", log);
        string load = ipc.RequestAsset("y.m2", 5000, true);
        Check(sent.Count == sentBefore + 1, "asset cache wiring: a load of the model being prefetched joins the prefetch", log);
        string pre = sent[sent.Count - 1];
        string preId = pre.Substring(pre.IndexOf("\"requestId\":\"") + 13);
        preId = preId.Substring(0, preId.IndexOf('"'));
        ipc.FeedForTest("assetResponse", preId, true, 5000, "y.m2", model, true, 0, null);
        Check(answered.Count == before + 1 && answered[answered.Count - 1].requestId == load,
              "asset cache wiring: the prefetch's answer goes to the load that joined it, and to nobody else", log);
        Check(sent.Count == sentBefore + 3 && sent[sent.Count - 2].Contains("5001") && sent[sent.Count - 1].Contains("5002"),
              "asset cache wiring: the prefetched model's skin and skeleton are asked for next", log);

        ipc.FeedForTest("loadWoWModel", null, false, 900, "x.m2", null, false, 8, null);
        ipc.RequestAssetByFileDataID(900);
        Check(ipc.Cache.Epoch == 8 && ipc.Cache.Count == 0 && sent.Count == sentBefore + 4,
              "asset cache wiring: another epoch asks the host again", log);
        UnityEngine.Object.DestroyImmediate(go);
    }

    // ---------------------------------------------------------------- the Models viewport's background

    static float Eotf(float s) { return s <= 0.04045f ? s / 12.92f : Mathf.Pow((s + 0.055f) / 1.055f, 2.4f); }
    static float Oetf(float l) { return l <= 0.0031308f ? l * 12.92f : 1.055f * Mathf.Pow(l, 1f / 2.4f) - 0.055f; }

    /// <summary>What a clear channel displays as, in sRGB 0..255, on the frame's path in the default 'full' mode: the
    /// buffer holds the authored value (Unity linearises the gamma'd clear), the frame decode applies the EOTF, the
    /// neutral LDR LUT of n nodes (8-bit node values, linear between them) and the swapchain's OETF follow.</summary>
    static float ShownThroughLut(float authored, int n)
    {
        float x = Mathf.Clamp01(Eotf(authored)) * (n - 1);
        int i = Mathf.Min((int)x, n - 2);
        float f = x - i;
        float lo = Mathf.Round(255f * i / (n - 1)) / 255f, hi = Mathf.Round(255f * (i + 1) / (n - 1)) / 255f;
        return Oetf(lo + (hi - lo) * f) * 255f;
    }

    /// <summary>
    /// The background message and launch argument as the player reads them, and the clear colour it derives
    /// (WmvMain.ViewportBackgroundClear), checked against a model of the frame's colour path: every byte comes back
    /// as itself through the 32-node LUT, the default keeps the exact float the viewport always cleared to, and
    /// without a LUT a colour is passed through unchanged. The model is the player's own arithmetic; what the GPU
    /// shows is measured on real captures by the host's test.
    /// </summary>
    static void BackgroundTests(Action<string> log)
    {
        Color32 c;
        Check(WmvIpcClient.ParseViewportBackground("{\"type\":\"viewportBackground\",\"r\":74,\"g\":111,\"b\":165}", out c)
              && c.r == 74 && c.g == 111 && c.b == 165 && c.a == 255,
              "background json: a viewportBackground line gives its opaque colour", log);
        Check(!WmvIpcClient.ParseViewportBackground("{\"type\":\"viewportBackground\",\"r\":256,\"g\":0,\"b\":0}", out c)
              && !WmvIpcClient.ParseViewportBackground("{\"type\":\"viewportBackground\",\"r\":0,\"g\":-1,\"b\":0}", out c),
              "background json: a channel outside 0..255 is refused", log);
        Check(!WmvIpcClient.ParseViewportBackground("{\"type\":\"runtimeState\",\"r\":1,\"g\":2,\"b\":3}", out c),
              "background json: a line of another type is not taken for a background", log);

        Check(WmvIpcClient.ParseBackgroundArgument("4A6FA5", out c) && c.r == 0x4A && c.g == 0x6F && c.b == 0xA5 && c.a == 255
              && WmvIpcClient.ParseBackgroundArgument("#4a6fa5", out c) && c.r == 0x4A && c.g == 0x6F && c.b == 0xA5,
              "background argument: RRGGBB in either case, with or without '#'", log);
        Check(!WmvIpcClient.ParseBackgroundArgument("4A6FA", out c) && !WmvIpcClient.ParseBackgroundArgument("4A6FA5F", out c)
              && !WmvIpcClient.ParseBackgroundArgument("GG6FA5", out c) && !WmvIpcClient.ParseBackgroundArgument("", out c)
              && !WmvIpcClient.ParseBackgroundArgument(null, out c) && !WmvIpcClient.ParseBackgroundArgument("#", out c),
              "background argument: a wrong length, a non-hex digit, empty and null are refused", log);

        const BindingFlags statics = BindingFlags.Static | BindingFlags.NonPublic;
        FieldInfo lutNodes = typeof(WmvMain).GetField("ClearLutNodes", statics);
        FieldInfo authoredDomain = typeof(WmvMain).GetField("ClearInAuthoredDomain", statics);
        MethodInfo clearOf = typeof(WmvMain).GetMethod("ViewportBackgroundClear", statics);
        MethodInfo channelOf = typeof(WmvMain).GetMethod("ClearChannel", statics);
        if (lutNodes == null || authoredDomain == null || clearOf == null || channelOf == null)
        {
            Check(false, "background clear: WmvMain's clear-colour members are where the test expects them", log);
            return;
        }
        object hadNodes = lutNodes.GetValue(null), hadDomain = authoredDomain.GetValue(null);
        try
        {
            lutNodes.SetValue(null, 32);
            authoredDomain.SetValue(null, true);
            int off = 0, worst = -1;
            float worstErr = 0f;
            for (int b = 0; b < 256; b++)
            {
                float shown = ShownThroughLut((float)channelOf.Invoke(null, new object[] { (byte)b }), 32);
                float err = Mathf.Abs(shown - b);
                if (Mathf.RoundToInt(shown) != b || err > 0.25f) off++;
                if (err > worstErr) { worstErr = err; worst = b; }
            }
            Check(off == 0, string.Format("background clear: through the 32-node LUT every byte 0..255 comes back as itself " +
                                          "(worst {0:F3} of a step, at {1})", worstErr, worst), log);
            int plainOff = 0;
            for (int b = 0; b < 256; b++)
                if (Mathf.RoundToInt(ShownThroughLut(b / 255f, 32)) != b) plainOff++;
            Check(plainOff > 0, string.Format("background clear: ... where the colour passed straight through would miss {0} " +
                                              "byte(s), so the compensation is doing the work", plainOff), log);

            Color def = (Color)clearOf.Invoke(null, new object[] { WmvMain.DefaultViewportBackground });
            Color legacy = new Color(0.10f, 0.10f, 0.12f).gamma;
            Check(def == legacy, "background clear: the default #19191E clears to the exact float the viewport always " +
                                 "cleared to, so its frames are unchanged", log);
            Check(Mathf.RoundToInt(ShownThroughLut(def.linear.r, 32)) == 25 && Mathf.RoundToInt(ShownThroughLut(def.linear.b, 32)) == 30,
                  "background clear: ... and that float shows as (25,25,30) through the LUT, as measured", log);
            Color grey = (Color)clearOf.Invoke(null, new object[] { new Color32(128, 128, 128, 255) });
            Check(Mathf.RoundToInt(ShownThroughLut(grey.linear.r, 32)) == 128 && grey.r == grey.g && grey.g == grey.b && grey.a == 1f,
                  "background clear: #808080 is an opaque neutral grey that shows as 128", log);

            lutNodes.SetValue(null, 0);
            authoredDomain.SetValue(null, false);
            Color plain = (Color)clearOf.Invoke(null, new object[] { new Color32(64, 128, 200, 255) });
            Check(Mathf.Abs(plain.r - 64 / 255f) < 1e-5f && Mathf.Abs(plain.g - 128 / 255f) < 1e-5f &&
                  Mathf.Abs(plain.b - 200 / 255f) < 1e-5f,
                  "background clear: with no LUT and no frame decode the colour is passed through as it is", log);
        }
        finally
        {
            lutNodes.SetValue(null, hadNodes);
            authoredDomain.SetValue(null, hadDomain);
        }
    }

    // ---------------------------------------------------------------- world models

    static bool SameInts(int[] a, int[] b)
    {
        if (a == null || b == null || a.Length != b.Length) return false;
        for (int i = 0; i < a.Length; i++) if (a[i] != b[i]) return false;
        return true;
    }

    /// <summary>
    /// The world-model builder on the synthetic "squares" WMO (WmoSynthetic): three groups, one batch
    /// each, material 0 an opaque textured shader-0 surface, material 1 two-sided (flag 0x04) with blend
    /// 1 and no texture. Checked through the real parser and builder: one object per group, one submesh
    /// per batch, the winding flipped exactly as the M2 path flips it, the material plans on the
    /// world-model shader, the verdicts, the converted bounds, a missing group, disposal, the per-row
    /// material checks (MapObjectMaterialRowTests), and the camera's world-model framing
    /// handing a model back its own planes.
    /// </summary>
    static void MapObjectTests(Action<string> log)
    {
        const uint firstGroup = 900000, texture = 910000;
        int liveBefore = WmvRuntimeMapObject.Live;
        WmoRoot root = WmoParser.ParseRoot(WmoSynthetic.SquaresRoot(3, firstGroup, texture), "squares root");
        var groups = new WmoGroup[3];
        for (int i = 0; i < 3; i++)
            groups[i] = WmoParser.ParseGroup(WmoSynthetic.SquaresGroup(i), "squares group " + i, i);
        var textures = new Dictionary<uint, WmvWmoTexture>
        {
            { texture, new WmvWmoTexture { FileDataID = texture, Image = FlatTexture(), Decoded = true, Width = 2, Height = 2 } },
        };

        WmvRuntimeMapObject rt = WmvWmoBuilder.Build(root, groups, textures, "WmoTest", null);
        Check(rt != null && rt.Root != null, "wmo: built", log);
        if (rt == null || rt.Root == null) return;
        Check(!rt.Root.activeSelf, "wmo: the root is built inactive (staged until adopted)", log);
        Check(rt.GroupObjects.Length == 3 && rt.Renderers == 3, "wmo: one object and one renderer per group (" + rt.Renderers + ")", log);
        bool oneSubmesh = true;
        for (int g = 0; g < 3; g++)
            oneSubmesh = oneSubmesh && rt.Meshes[g] != null && rt.Meshes[g].subMeshCount == 1 && rt.SubmeshTriangles[g].Length == 1;
        Check(oneSubmesh, "wmo: one submesh per MOBA batch", log);
        Check(rt.Submeshes == 3 && rt.Batches == 3 && rt.TriangleCount == 6 && rt.VertexCount == 12,
              "wmo: counts (submeshes " + rt.Submeshes + ", triangles " + rt.TriangleCount + ", vertices " + rt.VertexCount + ")", log);
        // MOVI (0,1,2, 0,2,3) with the second and third index of each triangle swapped.
        Check(SameInts(rt.SubmeshTriangles[0][0], new[] { 0, 2, 1, 0, 3, 2 }), "wmo: batch winding flipped like the M2 path", log);
        Check(SameInts(rt.Meshes[0].GetTriangles(0), new[] { 0, 2, 1, 0, 3, 2 }), "wmo: the mesh holds the flipped triangles", log);
        Check(rt.SubmeshMaterialIds[0][0] == 0 && rt.SubmeshMaterialIds[1][0] == 1,
              "wmo: batch material ids follow the 0x2 rule (group 0 -> 0, group 1 -> 1)", log);

        Material opaque = rt.Materials.Length > 0 ? rt.Materials[0] : null;
        Material keyed = rt.Materials.Length > 1 ? rt.Materials[1] : null;
        Check(opaque != null && keyed != null && rt.ResolvedMaterials == 1 && rt.PartialMaterials == 0 && rt.UnresolvedMaterials == 1 &&
              rt.ProvisionalMaterials == 1,
              "wmo: one material per used MOMT entry, the empty-+0x0C one a labelled fallback (resolved " + rt.ResolvedMaterials +
              ", partial " + rt.PartialMaterials + ", unresolved " + rt.UnresolvedMaterials + ", provisional " + rt.ProvisionalMaterials + ")", log);
        Check(WmvWmoBuilder.ResolveMapObjectShader(null) != null, "wmo: the world-model shader ships in Resources", log);
        if (opaque != null && keyed != null)
        {
            Check(opaque.shader != null && opaque.shader.name == "WMV/Map Object",
                  "wmo: materials use the world-model shader, not the M2 combiner shader", log);
            Check(opaque.GetTexture("_WmoTex0") != null && opaque.GetFloat("_WmoUv0") == 0f &&
                  opaque.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.Diffuse,
                  "wmo: id 0 binds slot +0x0C to register t0 on UV channel 0, permutation diffuse", log);
            Check(opaque.renderQueue == (int)UnityEngine.Rendering.RenderQueue.Geometry && opaque.GetFloat("_AlphaTest") == 0f &&
                  opaque.GetFloat("_ZWrite") == 1f && opaque.GetTag("RenderType", false, "") == "Opaque" &&
                  opaque.GetFloat("_SrcBlend") == (float)(int)UnityEngine.Rendering.BlendMode.One &&
                  opaque.GetFloat("_DstBlend") == (float)(int)UnityEngine.Rendering.BlendMode.Zero &&
                  opaque.GetFloat("_SrcBlendA") == (float)(int)UnityEngine.Rendering.BlendMode.One &&
                  opaque.GetFloat("_DstBlendA") == (float)(int)UnityEngine.Rendering.BlendMode.Zero,
                  "wmo: blend 0 is One/Zero with depth write, no test, Geometry queue", log);
            Check(opaque.GetFloat("_Cull") == (float)UnityEngine.Rendering.CullMode.Back, "wmo: flag 0x04 clear culls back faces", log);
            Check(keyed.GetTexture("_WmoTex0") == null, "wmo: empty slot +0x0C keeps the register's white default", log);
            Check(keyed.renderQueue == (int)UnityEngine.Rendering.RenderQueue.AlphaTest && keyed.GetFloat("_AlphaTest") == 1f &&
                  Mathf.Abs(keyed.GetFloat("_Cutoff") - 128f / 255f) < 1e-4f && keyed.GetFloat("_ZWrite") == 1f &&
                  keyed.GetTag("RenderType", false, "") == "TransparentCutout",
                  "wmo: blend 1 is the client's 128/255 key with depth write", log);
            Check(keyed.GetFloat("_Cull") == (float)UnityEngine.Rendering.CullMode.Off, "wmo: flag 0x04 turns culling off", log);
            Check(opaque.GetFloat("_WmoLightBypass") == 0f && opaque.GetFloat("_WmoVertexColourDiag") == 0f,
                  "wmo: no light bypass without F_UNLIT, and no vertex colour in normal rendering", log);
        }
        Check(rt.Textures.Length == 1, "wmo: one Texture2D for the one referenced file", log);
        Check(rt.MaterialInfo[0].Verdict == "resolved" && rt.MaterialInfo[1].Verdict == "unresolved: U-23b" &&
              rt.MaterialInfo[1].Plan.ProvisionalFallback && rt.EmptySamplerSlots == 1,
              "wmo: material 0 resolved, material 1 (+0x0C empty: the whole surface is an unbound register) unresolved U-23b", log);

        // Group 2 spans WoW x 24..34, y 0..10, z 0 -> Unity x = -y, y = z, z = x.
        Bounds b = rt.Bounds;
        Check(rt.HasBounds && Near(b.min.x, -10f) && Near(b.max.x, 0f) && Near(b.min.y, 0f) && Near(b.max.y, 0f) &&
              Near(b.min.z, 0f) && Near(b.max.z, 34f), "wmo: bounds are the converted drawn geometry (" + b.min + " .. " + b.max + ")", log);
        Check(Near(rt.Meshes[0].uv[2].x, 1f) && Near(rt.Meshes[0].uv[2].y, 0f), "wmo: UV set 0 converted with V flipped", log);
        rt.Dispose();
        Check(rt.Root == null && WmvRuntimeMapObject.Live == liveBefore, "wmo: dispose releases the runtime (live " + WmvRuntimeMapObject.Live + ")", log);
        rt.Dispose();
        Check(WmvRuntimeMapObject.Live == liveBefore, "wmo: a second dispose does not count twice", log);

        // A group whose file never arrived: its slot stays empty, the others still build.
        groups[1] = null;
        WmvRuntimeMapObject partial = WmvWmoBuilder.Build(root, groups, textures, "WmoTestMissing", null);
        Check(partial.GroupsMissing == 1 && partial.Renderers == 2 && partial.GroupObjects[1] == null,
              "wmo: a missing group builds nothing and the rest still build", log);
        partial.Dispose();

        // Verdicts on data the established rules do not cover.
        WmoMaterial m23 = MaterialFrom(WmoSynthetic.Material(0, 23, 0, 0, 5001), 0);
        WmvWmoMaterialInfo v23 = WmvWmoBuilder.ClassifyMaterial(m23);
        Check(v23.Unresolved && v23.Plan.Resolution == WmoResolution.Unresolved && v23.Plan.ProvisionalFallback &&
              v23.Plan.Permutation == WmoPermutation.FourLayer && v23.PrimaryTexture == 0 && v23.TextureSlots[1] == 5001,
              "wmo: shader 23 with a layer but no height map is unresolved (U-23b), drawn by the labelled fallback", log);
        WmoMaterial blend2 = MaterialFrom(WmoSynthetic.Material(0, 0, 2, 5002), 1);
        WmvWmoMaterialInfo vb = WmvWmoBuilder.ClassifyMaterial(blend2);
        Check(vb.Unresolved && vb.Cutout && vb.TrueBlend && vb.Verdict == "unresolved: U-B2,U-B3,U-B4,U-B5",
              "wmo: blend 2 is unresolved (U-B2..U-B5) and drawn as the provisional key", log);

        MapObjectMaterialRowTests(log);
        MapObjectFourLayerTests(log);
        MapObjectTwoLayerAndOpaqueTests(log);
        MapObjectEnvMetalTests(log);
        MapObjectUnlitInteriorTests(log);
        MapObjectShaderRenderTests(log);

        // Framing: a world model frames with planes that cover it, and a model gets its own back.
        GameObject go;
        WmvOrbitCamera cam = NewCamera(out go);
        var big = new Bounds(new Vector3(100f, 50f, -20f), new Vector3(15000f, 3000f, 15000f));
        cam.FrameMapObject(big);
        Camera c = go.GetComponent<Camera>();
        float radius = big.extents.magnitude;
        Check(cam.MapObjectPlanes && c.farClipPlane >= cam.distance + radius, "wmo: far plane covers the whole object when framed (" + c.farClipPlane + ")", log);
        for (int n = 0; n < 400; n++) cam.ZoomByNotches(1f);
        for (int f = 0; f < 400; f++) cam.AdvanceZoom(1f / 60f);
        // AdvanceZoom only moves the distance; the frame loop re-applies the view. SetView with no
        // distance scale re-applies it here without touching the distance, so the planes checked are
        // the ones of the zoomed camera and not the framed one's.
        cam.SetView(cam.yaw, cam.pitch, 0f);
        Check(c.farClipPlane >= cam.distance + radius, "wmo: ... and still at the closest zoom (distance " + cam.distance + ")", log);
        Check(cam.MinDistance < cam.FramedDistance * 0.02f, "wmo: a world model zooms closer than a model's 2% floor", log);
        // A pan moves the pivot off the object's centre; zoomed in there, far must still reach the far
        // side of the OBJECT, which is further away than distance + radius.
        cam.FrameMapObject(big);
        cam.pivot += Vector3.right * radius;
        for (int n = 0; n < 400; n++) cam.ZoomByNotches(1f);
        for (int f = 0; f < 400; f++) cam.AdvanceZoom(1f / 60f);
        cam.SetView(cam.yaw, cam.pitch, 0f);
        float reach = Vector3.Distance(go.transform.position, big.center) + radius;
        Check(c.farClipPlane >= reach && reach > cam.distance + radius,
              "wmo: far plane covers the whole object with the pivot panned off-centre (far " + c.farClipPlane + ", reach " + reach + ")", log);
        cam.Frame(new Bounds(Vector3.zero, Vector3.one * 2f));
        Check(!cam.MapObjectPlanes && Mathf.Abs(cam.MinDistance / cam.FramedDistance - 0.02f) < 1e-3f &&
              Mathf.Abs(c.farClipPlane - Mathf.Max(100f, cam.distance * 20f)) < 1e-3f,
              "wmo: framing a model restores the model's zoom range and clip planes", log);
        UnityEngine.Object.DestroyImmediate(go);
    }

    /// <summary>
    /// One synthetic WMO whose eight groups each draw one material, one per material-table row this stage
    /// implements or deliberately leaves provisional, through the real parser, fetch policy and builder:
    /// clamp U, clamp V, id 16 keyed with F_UNLIT, blend 2 (unresolved), shader 23 with blend 2 (unresolved,
    /// its four-layer arithmetic in the labelled opaque fallback), plain id 0, id 5 with F_UNLIT (its
    /// diffuse part, the bypass not honoured) and shader 23 with an env map and no layer (the baseline,
    /// its env map neither bound nor decoded). Read back from the created materials: permutation, register
    /// binding and UV channel, alpha test, queue, cull, per-axis addressing, the light bypass, the upload
    /// sharing and GPU-only uploads, and the decode set. The complete four-layer rows have their own WMO
    /// (MapObjectFourLayerTests), because they need MOC2.
    /// </summary>
    static void MapObjectMaterialRowTests(Action<string> log)
    {
        const uint T = 920000, T2 = 920001, T3 = 920002, TEnv = 920003;
        byte[][] records =
        {
            WmoSynthetic.Material(0x40, 0, 0, T),                                    // 0 clamp U
            WmoSynthetic.Material(0x80, 0, 0, T),                                    // 1 clamp V
            WmoSynthetic.Material(0x01, 16, 1, T),                                   // 2 id 16, key, F_UNLIT
            WmoSynthetic.Material(0xC4, 0, 2, T),                                    // 3 blend 2, two-sided, clamp flags
            WmoSynthetic.Material(0, 23, 2, 0, T2, T3, new uint[] { T2, T3, T2, T3, T2, T3 }), // 4 shader 23, blend 2, +0x0C empty
            WmoSynthetic.Material(0, 0, 0, T),                                       // 5 plain id 0
            WmoSynthetic.Material(0x01, 5, 0, T, T2),                                // 6 id 5 with F_UNLIT
            WmoSynthetic.Material(0, 23, 1, TEnv),                                   // 7 shader 23, env map, no layer
        };
        int n = records.Length;
        var spec = new WmoSynthetic.RootSpec
        {
            Materials = records,
            GroupInfos = new byte[n][],
            GroupFileDataIDs = new uint[n],
            GroupNames = new string[0],
        };
        var groups = new WmoGroup[n];
        for (int i = 0; i < n; i++)
        {
            spec.GroupInfos[i] = WmoSynthetic.GroupInfo(WmoGroupFlags.Outdoor, new WowVec3(12f * i, 0f, 0f),
                                                        new WowVec3(12f * i + 10f, 10f, 0f), 0);
            spec.GroupFileDataIDs[i] = 930000u + (uint)i;
            float x = 12f * i;
            var gs = new WmoSynthetic.GroupSpec
            {
                Positions = new[] { x, 0f, 0f, x + 10f, 0f, 0f, x + 10f, 10f, 0f, x, 10f, 0f },
                Normals = new[] { 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f },
                Indices = new ushort[] { 0, 1, 2, 0, 2, 3 },
                Batches = new[] { WmoSynthetic.Batch(0, 6, 0, 3, WmoBatch.FlagLargeMaterialId, i) },
                Mpy2 = new byte[] { 0x20, 0, (byte)i, 0, 0x20, 0, (byte)i, 0 },
            };
            gs.TexCoordSets.Add(new[] { 0f, 0f, 1f, 0f, 1f, 1f, 0f, 1f });
            groups[i] = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(gs), "row group " + i, i);
        }
        WmoRoot root = WmoParser.ParseRoot(WmoSynthetic.BuildRoot(spec), "row root");

        // The decode set the load computes: only what the drawn plans sample.
        var sampled = new HashSet<uint>();
        for (int i = 0; i < n; i++)
            WmoMaterialSemantics.CollectSampledTextures(WmoMaterialSemantics.Plan(root.Materials[i]), sampled);
        Check(sampled.Count == 3 && sampled.Contains(T) && sampled.Contains(T2) && sampled.Contains(T3) && !sampled.Contains(TEnv),
              "wmo rows: the decode set is what the plans sample (the blend-2 id-23 layers and heights; never an id-23 env map)", log);

        var textures = new Dictionary<uint, WmvWmoTexture>
        {
            { T, new WmvWmoTexture { FileDataID = T, Image = FlatTexture(), Decoded = true, Width = 2, Height = 2 } },
            { T2, new WmvWmoTexture { FileDataID = T2, Image = FlatTexture(), Decoded = true, Width = 2, Height = 2 } },
            { T3, new WmvWmoTexture { FileDataID = T3, Image = FlatTexture(), Decoded = true, Width = 2, Height = 2 } },
            { TEnv, new WmvWmoTexture { FileDataID = TEnv, HeaderOnly = true, Width = 2, Height = 2 } },
        };
        WmvRuntimeMapObject rt = WmvWmoBuilder.Build(root, groups, textures, "WmoRows", null);
        Check(rt != null && rt.Materials.Length == n, "wmo rows: built", log);
        if (rt == null || rt.Materials.Length != n) return;
        bool all = true;
        foreach (Material m in rt.Materials) all = all && m != null && m.shader != null && m.shader.name == "WMV/Map Object";
        Check(all, "wmo rows: every material, provisional ones included, uses the world-model shader", log);
        if (!all) { rt.Dispose(); return; }

        Material mClampU = rt.Materials[0], mClampV = rt.Materials[1], mKey16 = rt.Materials[2], mBlend2 = rt.Materials[3];
        Material m23 = rt.Materials[4], mPlain = rt.Materials[5], mId5 = rt.Materials[6], m23Env = rt.Materials[7];
        var tClampU = mClampU.GetTexture("_WmoTex0") as Texture2D;
        var tClampV = mClampV.GetTexture("_WmoTex0") as Texture2D;
        var tKey16 = mKey16.GetTexture("_WmoTex0") as Texture2D;
        var tBlend2 = mBlend2.GetTexture("_WmoTex0") as Texture2D;
        var tPlain = mPlain.GetTexture("_WmoTex0") as Texture2D;
        var tId5 = mId5.GetTexture("_WmoTex0") as Texture2D;

        Check(tClampU != null && tClampU.wrapModeU == TextureWrapMode.Clamp && tClampU.wrapModeV == TextureWrapMode.Repeat,
              "wmo rows: flag 0x40 clamps U and repeats V", log);
        Check(tClampV != null && tClampV.wrapModeU == TextureWrapMode.Repeat && tClampV.wrapModeV == TextureWrapMode.Clamp,
              "wmo rows: flag 0x80 repeats U and clamps V", log);
        Check(tPlain != null && tPlain.wrapModeU == TextureWrapMode.Repeat && tPlain.wrapModeV == TextureWrapMode.Repeat,
              "wmo rows: no clamp flag repeats both axes", log);
        Check(mKey16.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.Diffuse && mKey16.GetFloat("_AlphaTest") == 1f &&
              mKey16.renderQueue == (int)UnityEngine.Rendering.RenderQueue.AlphaTest && mKey16.GetFloat("_WmoLightBypass") == 1f,
              "wmo rows: id 16 blend 1 with F_UNLIT is the diffuse permutation, keyed, light bypassed", log);
        Check(mBlend2.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.ProvisionalBaseline && mBlend2.GetFloat("_AlphaTest") == 1f &&
              mBlend2.GetFloat("_Cull") == (float)UnityEngine.Rendering.CullMode.Off && mBlend2.GetFloat("_ZWrite") == 1f &&
              tBlend2 != null && tBlend2.wrapModeU == TextureWrapMode.Repeat && tBlend2.wrapModeV == TextureWrapMode.Repeat &&
              rt.MaterialInfo[3].Plan.Resolution == WmoResolution.Unresolved,
              "wmo rows: blend 2 is unresolved: provisional key, cull from 0x04, its clamp flags not applied", log);
        Check(m23.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.FourLayer && m23.GetFloat("_AlphaTest") == 0f &&
              m23.GetTexture("_WmoTex0") == null && m23.GetTexture("_WmoTex1") != null && m23.GetTexture("_WmoTex8") != null &&
              m23.GetFloat("_ZWrite") == 1f && m23.GetFloat("_SrcBlend") == (float)UnityEngine.Rendering.BlendMode.One &&
              m23.GetFloat("_DstBlend") == (float)UnityEngine.Rendering.BlendMode.Zero &&
              rt.MaterialInfo[4].Plan.ProvisionalFallback &&
              rt.MaterialInfo[4].Verdict == "unresolved: U-B2,U-B3,U-B4,U-B5,U-23a,U-E2,U-E3,U-E4,U-P1,U-V4",
              "wmo rows: shader 23 with blend 2 keeps its four-layer arithmetic in the labelled opaque fallback, env map unbound " +
              "(U-V4: its one-set group has no MOC2)", log);
        Check(m23Env.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.ProvisionalBaseline && m23Env.GetTexture("_WmoTex0") == null &&
              rt.MaterialInfo[7].Verdict == "unresolved: U-23b",
              "wmo rows: shader 23 without a layer is the baseline drawing the register's white, its env map never bound as a diffuse", log);
        Check(mId5.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.EnvMetal && mId5.GetFloat("_WmoLightBypass") == 0f &&
              mId5.GetTexture("_WmoTex1") == null && rt.MaterialInfo[6].Verdict == "resolved-partial: U-G1,U-E2,U-E3,U-E4,U-P1,U-F1",
              "wmo rows: id 5 with F_UNLIT draws its diffuse part without the bypass (its emissive is not drawn, U-F1)", log);
        Check(mPlain.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.Diffuse && mPlain.GetFloat("_WmoLightBypass") == 0f &&
              mPlain.GetFloat("_AlphaTest") == 0f, "wmo rows: plain id 0 is the lit, untested diffuse permutation", log);

        // Uploads: one per (file, U, V), alpha kept. File T three times -- clamp U, clamp V, repeat (the id-16
        // key, the blend-2 key, plain id 0 and id 5's diffuse share the last) -- plus T2 and T3 for the id-23
        // layers and heights; none keeps a CPU-readable copy.
        bool gpuOnly = true;
        foreach (Texture2D t in rt.Textures) gpuOnly = gpuOnly && t != null && !t.isReadable;
        Check(rt.Textures.Length == 5 && ReferenceEquals(tKey16, tBlend2) && ReferenceEquals(tPlain, tId5) && ReferenceEquals(tKey16, tPlain) &&
              !ReferenceEquals(tClampU, tClampV) && !ReferenceEquals(tPlain, tClampU) && gpuOnly,
              "wmo rows: uploads are shared per (file, U, V) and GPU-only -- 3 textures for one file, 5 in all (" + rt.Textures.Length + ")", log);
        Check(rt.ResolvedMaterials == 4 && rt.UnresolvedMaterials == 3 && rt.PartialMaterials == 1 &&
              rt.ProvisionalMaterials == 3 && rt.EmptySamplerSlots == 0 && rt.PrimaryTexturesMissing == 0,
              "wmo rows: counters (resolved " + rt.ResolvedMaterials + ", unresolved " +
              rt.UnresolvedMaterials + ", provisional " + rt.ProvisionalMaterials + ", empty slots " + rt.EmptySamplerSlots + ")", log);

        string diag = WmvWmoBuilder.DescribeMaterialDiag(rt.MaterialInfo[0], mClampU, 1, textures);
        Check(diag.Contains("t0 diffuse <- +0x0C " + T + " @ UV0") && diag.Contains("readback: shader 'WMV/Map Object', queue 2000") &&
              diag.Contains("_WmoPermutation 1") && diag.Contains("wrap Clamp/Repeat") && diag.Contains("| RESOLVED"),
              "wmo rows: the material diagnostic reads the plan and the created material back", log);
        rt.Dispose();
    }

    /// <summary>
    /// The four-layer permutation through the real parser, fetch policy and builder, on a synthetic WMO of
    /// four groups: g0 draws a complete id-23 material with non-contiguous layers (1011, its layer-2 height
    /// present but unread), four MOTV sets and MOC2; g1 an id-23 material with a layer and no height map (the
    /// U-23b fallback); g2 a plain id 0 with one MOTV set; g3 the complete material again in a group with one
    /// MOTV set and no MOC2 (a stream gap). Read back: decode set, register bindings and UV channels 0..3,
    /// the layer mask, the unbound env and empty-layer registers, upload sharing between a layer and a height
    /// map, the MOC2 channel's values and where it is uploaded, the counters and the diagnostic line.
    /// </summary>
    static void MapObjectFourLayerTests(Action<string> log)
    {
        const uint Env = 940000, TA = 940001, TB = 940002, TC = 940003;
        byte[][] records =
        {
            WmoSynthetic.Material(0, 23, 0, Env, TA, 0, new uint[] { TB, TA, TA, TC, TB, TA }),   // 0 layers 1011, heights 1111
            WmoSynthetic.Material(0, 23, 0, 0, TB, 0, new uint[] { 0, 0, 0, 0, 0, 0 }),          // 1 layer 1, no height: U-23b
            WmoSynthetic.Material(0, 0, 0, TA),                                                  // 2 plain id 0
        };
        int[] groupMaterial = { 0, 1, 2, 0 };
        int[] groupUvSets = { 4, 4, 1, 1 };
        int n = groupMaterial.Length;
        var spec = new WmoSynthetic.RootSpec
        {
            Materials = records,
            GroupInfos = new byte[n][],
            GroupFileDataIDs = new uint[n],
            GroupNames = new string[0],
        };
        var groups = new WmoGroup[n];
        // MOC2 per vertex as stored (B, G, R, A): vertex 0 all layer 1 (byte 2), vertex 1 a layer-2 weight on
        // the empty layer (byte 1) and a non-zero byte 3, vertex 2 all layer 3 (byte 0), vertex 3 all layer 4.
        byte[] moc2 = { 0, 0, 255, 0,   0, 200, 55, 9,   255, 0, 0, 0,   0, 0, 0, 0 };
        for (int i = 0; i < n; i++)
        {
            spec.GroupInfos[i] = WmoSynthetic.GroupInfo(WmoGroupFlags.Outdoor, new WowVec3(12f * i, 0f, 0f),
                                                        new WowVec3(12f * i + 10f, 10f, 0f), 0);
            spec.GroupFileDataIDs[i] = 950000u + (uint)i;
            float x = 12f * i;
            var gs = new WmoSynthetic.GroupSpec
            {
                Positions = new[] { x, 0f, 0f, x + 10f, 0f, 0f, x + 10f, 10f, 0f, x, 10f, 0f },
                Normals = new[] { 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f },
                Indices = new ushort[] { 0, 1, 2, 0, 2, 3 },
                Batches = new[] { WmoSynthetic.Batch(0, 6, 0, 3, WmoBatch.FlagLargeMaterialId, groupMaterial[i]) },
                Mpy2 = new byte[] { 0x20, 0, (byte)groupMaterial[i], 0, 0x20, 0, (byte)groupMaterial[i], 0 },
                Moc2 = groupUvSets[i] == 4 ? moc2 : null,
            };
            // Set k is offset by k so a sampler on the wrong set reads a different coordinate.
            for (int s = 0; s < groupUvSets[i]; s++)
                gs.TexCoordSets.Add(new[] { s, 0f, 1f + s, 0f, 1f + s, 1f, s, 1f });
            groups[i] = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(gs), "four-layer group " + i, i);
        }
        WmoRoot root = WmoParser.ParseRoot(WmoSynthetic.BuildRoot(spec), "four-layer root");

        var sampled = new HashSet<uint>();
        for (int i = 0; i < records.Length; i++)
            WmoMaterialSemantics.CollectSampledTextures(WmoMaterialSemantics.Plan(root.Materials[i]), sampled);
        Check(sampled.Count == 2 && sampled.Contains(TA) && sampled.Contains(TB),
              "wmo 23 rows: the decode set is the layers and heights read (not the env map, not the empty layer's height)", log);

        var textures = new Dictionary<uint, WmvWmoTexture>
        {
            { TA, new WmvWmoTexture { FileDataID = TA, Image = FlatTexture(), Decoded = true, Width = 2, Height = 2 } },
            { TB, new WmvWmoTexture { FileDataID = TB, Image = FlatTexture(), Decoded = true, Width = 2, Height = 2 } },
            { TC, new WmvWmoTexture { FileDataID = TC, HeaderOnly = true, Width = 2, Height = 2 } },
            { Env, new WmvWmoTexture { FileDataID = Env, HeaderOnly = true, Width = 2, Height = 2 } },
        };
        var lines = new List<string>();
        WmvRuntimeMapObject rt = WmvWmoBuilder.Build(root, groups, textures, "WmoFourLayer", s => lines.Add(s));
        Check(rt != null && rt.Materials.Length == 3 && rt.Materials[0] != null && rt.Materials[1] != null,
              "wmo 23 rows: built", log);
        if (rt == null || rt.Materials.Length != 3 || rt.Materials[0] == null || rt.Materials[1] == null) return;

        Material m = rt.Materials[0], fb = rt.Materials[1];
        // Registers 2 and 6 (the empty layer 2 and its height) are not bound, so only the others are set.
        bool uv = true;
        foreach (int r in new[] { 1, 3, 4, 5, 7, 8 })
            uv = uv && m.GetFloat("_WmoUv" + r) == (float)((r - 1) % 4);
        Check(m.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.FourLayer && uv && m.GetFloat("_AlphaTest") == 0f &&
              m.renderQueue == (int)UnityEngine.Rendering.RenderQueue.Geometry,
              "wmo 23 rows: four-layer permutation, bound registers 1/3/4 and 5/7/8 on UV channels 0/2/3, untested, Geometry queue", log);
        Vector4 mask = m.GetVector("_WmoLayerMask");
        Check(mask.x == 1f && mask.y == 0f && mask.z == 1f && mask.w == 1f, "wmo 23 rows: layer mask (1,0,1,1) for layers 1011", log);
        var l1 = m.GetTexture("_WmoTex1") as Texture2D;
        var h1 = m.GetTexture("_WmoTex5") as Texture2D;
        var l3 = m.GetTexture("_WmoTex3") as Texture2D;
        Check(l1 != null && l3 != null && ReferenceEquals(l1, h1) && ReferenceEquals(l1, m.GetTexture("_WmoTex4")) &&
              ReferenceEquals(l3, m.GetTexture("_WmoTex7")) && m.GetTexture("_WmoTex0") == null &&
              m.GetTexture("_WmoTex2") == null && m.GetTexture("_WmoTex6") == null,
              "wmo 23 rows: a file used as a layer and a height map shares one upload; env, empty layer and its height unbound", log);
        Check(fb.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.FourLayer && fb.GetTexture("_WmoTex5") == null &&
              fb.GetTexture("_WmoTex1") != null && fb.GetVector("_WmoLayerMask").x == 1f && fb.GetVector("_WmoLayerMask").y == 0f &&
              rt.MaterialInfo[1].Plan.ProvisionalFallback && rt.MaterialInfo[1].Verdict == "unresolved: U-23b,U-23a,U-E2,U-E3,U-E4,U-P1",
              "wmo 23 rows: a layer without its height map is the labelled fallback, the height register left white", log);

        var weights = new List<Vector4>();
        rt.Meshes[0].GetUVs(WmvWmoBuilder.Moc2UvChannel, weights);
        Check(weights.Count == 4 && Near(weights[0].x, 1f) && Near(weights[0].y, 0f) && Near(weights[0].z, 0f) &&
              Near(weights[1].x, 55f / 255f) && Near(weights[1].y, 200f / 255f) && Near(weights[1].w, 9f / 255f) &&
              Near(weights[2].z, 1f) && Near(weights[3].x + weights[3].y + weights[3].z, 0f),
              "wmo 23 rows: MOC2 uploads as (byte 2, byte 1, byte 0, byte 3) / 255", log);
        var none = new List<Vector4>();
        rt.Meshes[2].GetUVs(WmvWmoBuilder.Moc2UvChannel, none);
        Check(none.Count == 0, "wmo 23 rows: a group without a four-layer batch gets no MOC2 channel", log);
        Check(rt.FourLayerMaterials == 2 && rt.FallbackMaterials == 1 && rt.PartialMaterials == 1 && rt.UnresolvedMaterials == 1 &&
              rt.ResolvedMaterials == 1 && rt.ProvisionalMaterials == 1 && rt.Moc2Groups == 3 && rt.StreamGapGroups == 1 &&
              rt.Textures.Length == 2 && rt.PrimaryTexturesMissing == 0 && rt.EmptySamplerSlots == 1,
              "wmo 23 rows: counters (four-layer " + rt.FourLayerMaterials + ", fallback " + rt.FallbackMaterials + ", MOC2 groups " +
              rt.Moc2Groups + ", stream gaps " + rt.StreamGapGroups + ", uploads " + rt.Textures.Length + ", empty slots " +
              rt.EmptySamplerSlots + ")", log);
        Check(rt.MaterialInfo[0].Moc2Vertices == 8 && rt.MaterialInfo[0].Moc2AlphaVertices == 1 &&
              rt.MaterialInfo[0].EmptyLayerWeightVertices == 1,
              "wmo 23 rows: per-material MOC2 exposure (U-23a byte 3, U-23b empty-layer weight) counted over drawn vertices", log);
        bool gapLogged = false;
        foreach (string line in lines) gapLogged = gapLogged || (line.Contains("group 3") && line.Contains("U-V4"));
        Check(gapLogged && rt.MaterialInfo[0].StreamGapVertices == 4 && rt.MaterialInfo[0].GapStreams == "MOC2 and MOTV set 4" &&
              rt.MaterialInfo[0].Verdict == "resolved-partial: U-23b,U-23a,U-E2,U-E3,U-E4,U-P1,U-V4",
              "wmo 23 rows: a four-layer batch in a group without MOC2 / MOTV sets is logged per group and as U-V4 on its material", log);
        string diag = WmvWmoBuilder.DescribeMaterialDiag(rt.MaterialInfo[0], m, 2, textures);
        Check(diag.Contains("_WmoLayerMask (1,0,1,1)") && diag.Contains("t5 (client t17) height 1") &&
              diag.Contains("RESOLVED-PARTIAL: U-23b,U-23a,U-E2,U-E3,U-E4,U-P1,U-V4") && diag.Contains("t0 " + Env + " not bound") &&
              diag.Contains("env map +0x0C " + Env + ": not decoded, not drawn") && diag.Contains("NOT drawn (U-E2, U-E3, U-E4, U-P1)"),
              "wmo 23 rows: the diagnostic reads back the mask, the client registers, the unbound env map and why its emissive is not drawn", log);
        rt.Dispose();
    }

    /// <summary>
    /// The two-layer (id 13) and opaque (id 4) permutations through the real parser, fetch policy and
    /// builder, on a synthetic WMO of five groups: g0 draws a complete id-13 material with blend 1, two MOTV
    /// sets and a lone MOCV set 2 (flag 0x4 clear); g1 an id-13 material whose +0x18 is empty (the U-23b
    /// fallback); g2 an id-4 material with blend 1 and flag 0x04, one MOTV set and no MOCV; g3 the complete
    /// id-13 material again in a group with two MOTV sets and no MOCV (a stream gap); g4 an id-4 material with
    /// blend 2 (the opaque fallback). Read back: decode set, permutations, register bindings and UV channels,
    /// no alpha test anywhere, the set-2 alpha channel's values and where it is uploaded, the gap default and
    /// its log line, upload sharing, the counters, the exposure counts and the diagnostic line.
    /// </summary>
    static void MapObjectTwoLayerAndOpaqueTests(Action<string> log)
    {
        const uint TA = 960001, TB = 960002;
        byte[][] records =
        {
            WmoSynthetic.Material(0, 13, 1, TA, TB),       // 0 complete id 13, blend 1
            WmoSynthetic.Material(0, 13, 0, TB, 0),        // 1 id 13 with +0x18 empty: U-23b fallback
            WmoSynthetic.Material(0x04, 4, 1, TA),         // 2 id 4, blend 1, two-sided
            WmoSynthetic.Material(0, 4, 2, TA),            // 3 id 4, blend 2: U-B2..U-B5 fallback
        };
        int[] groupMaterial = { 0, 1, 2, 0, 3 };
        int[] groupUvSets = { 2, 2, 1, 2, 1 };
        bool[] groupSet2 = { true, true, false, false, false };
        int n = groupMaterial.Length;
        var spec = new WmoSynthetic.RootSpec
        {
            Materials = records,
            GroupInfos = new byte[n][],
            GroupFileDataIDs = new uint[n],
            GroupNames = new string[0],
        };
        var groups = new WmoGroup[n];
        // MOCV set 2 as stored (B, G, R, A): the RGB is noise the permutation must ignore; the alphas are
        // va = 1, 128/255, 0 and 64/255.
        byte[] set2 = { 9, 8, 7, 255,   50, 60, 70, 128,   255, 255, 255, 0,   1, 2, 3, 64 };
        for (int i = 0; i < n; i++)
        {
            spec.GroupInfos[i] = WmoSynthetic.GroupInfo(WmoGroupFlags.Outdoor, new WowVec3(12f * i, 0f, 0f),
                                                        new WowVec3(12f * i + 10f, 10f, 0f), 0);
            spec.GroupFileDataIDs[i] = 970000u + (uint)i;
            float x = 12f * i;
            var gs = new WmoSynthetic.GroupSpec
            {
                Flags = WmoGroupFlags.Outdoor | (groupSet2[i] ? WmoGroupFlags.ColorSet2 : 0u) |
                        (groupUvSets[i] >= 2 ? WmoGroupFlags.TwoTexCoordSets : 0u),
                Positions = new[] { x, 0f, 0f, x + 10f, 0f, 0f, x + 10f, 10f, 0f, x, 10f, 0f },
                Normals = new[] { 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f },
                Indices = new ushort[] { 0, 1, 2, 0, 2, 3 },
                Batches = new[] { WmoSynthetic.Batch(0, 6, 0, 3, WmoBatch.FlagLargeMaterialId, groupMaterial[i]) },
                Mpy2 = new byte[] { 0x20, 0, (byte)groupMaterial[i], 0, 0x20, 0, (byte)groupMaterial[i], 0 },
            };
            if (groupSet2[i])
                gs.ColorSets.Add(set2);
            for (int t = 0; t < groupUvSets[i]; t++)
                gs.TexCoordSets.Add(new[] { t, 0f, 1f + t, 0f, 1f + t, 1f, t, 1f });
            groups[i] = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(gs), "two-layer group " + i, i);
        }
        WmoRoot root = WmoParser.ParseRoot(WmoSynthetic.BuildRoot(spec), "two-layer root");
        Check(groups[0].ColorSet2 != null && groups[0].ColorSet1 == null && groups[0].ColorSet2.Count == 4,
              "wmo 13 rows: a lone MOCV with flag 0x4 clear parses as set 2", log);

        var sampled = new HashSet<uint>();
        for (int i = 0; i < records.Length; i++)
            WmoMaterialSemantics.CollectSampledTextures(WmoMaterialSemantics.Plan(root.Materials[i]), sampled);
        Check(sampled.Count == 2 && sampled.Contains(TA) && sampled.Contains(TB),
              "wmo 13 rows: the decode set is the two files the plans sample, each once", log);

        var textures = new Dictionary<uint, WmvWmoTexture>
        {
            { TA, new WmvWmoTexture { FileDataID = TA, Image = FlatTexture(), Decoded = true, Width = 2, Height = 2 } },
            { TB, new WmvWmoTexture { FileDataID = TB, Image = FlatTexture(), Decoded = true, Width = 2, Height = 2 } },
        };
        var lines = new List<string>();
        WmvRuntimeMapObject rt = WmvWmoBuilder.Build(root, groups, textures, "WmoTwoLayer", s2 => lines.Add(s2));
        bool built = rt != null && rt.Materials.Length == 4;
        for (int i = 0; built && i < 4; i++) built = rt.Materials[i] != null;
        Check(built, "wmo 13 rows: built", log);
        if (!built) return;

        Material m13 = rt.Materials[0], fb13 = rt.Materials[1], m4 = rt.Materials[2], fb4 = rt.Materials[3];
        Check(m13.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.TwoLayer && m13.GetFloat("_WmoUv0") == 0f &&
              m13.GetFloat("_WmoUv1") == 1f && m13.GetFloat("_AlphaTest") == 0f &&
              m13.renderQueue == (int)UnityEngine.Rendering.RenderQueue.Geometry && rt.MaterialInfo[0].Verdict == "resolved-partial: U-V4",
              "wmo 13 rows: id 13 blend 1 is the two-layer permutation, t0 on UV0 and t1 on UV1, untested, Geometry queue " +
              "(resolved-partial only because g3 lacks MOCV set 2)", log);
        var t0 = m13.GetTexture("_WmoTex0") as Texture2D;
        var t1 = m13.GetTexture("_WmoTex1") as Texture2D;
        Check(t0 != null && t1 != null && !ReferenceEquals(t0, t1) && m13.GetTexture("_WmoTex2") == null,
              "wmo 13 rows: registers 0 and 1 bound to +0x0C and +0x18, nothing else", log);
        Check(fb13.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.TwoLayer && fb13.GetTexture("_WmoTex0") != null &&
              fb13.GetTexture("_WmoTex1") == null && rt.MaterialInfo[1].Plan.ProvisionalFallback &&
              rt.MaterialInfo[1].Verdict == "unresolved: U-23b",
              "wmo 13 rows: an empty +0x18 is the labelled fallback, its register left white", log);
        Check(m4.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.Opaque && m4.GetFloat("_AlphaTest") == 0f &&
              m4.GetFloat("_Cull") == (float)UnityEngine.Rendering.CullMode.Off && m4.GetFloat("_ZWrite") == 1f &&
              m4.renderQueue == (int)UnityEngine.Rendering.RenderQueue.Geometry && rt.MaterialInfo[2].Verdict == "resolved",
              "wmo 4 rows: id 4 blend 1 is the opaque permutation, never clipped, two-sided from 0x04", log);
        Check(fb4.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.Opaque && fb4.GetFloat("_AlphaTest") == 0f &&
              fb4.GetFloat("_SrcBlend") == (float)UnityEngine.Rendering.BlendMode.One &&
              fb4.GetFloat("_DstBlend") == (float)UnityEngine.Rendering.BlendMode.Zero &&
              rt.MaterialInfo[3].Plan.ProvisionalFallback && rt.MaterialInfo[3].Verdict == "unresolved: U-B2,U-B3,U-B4,U-B5",
              "wmo 4 rows: id 4 blend 2 is the labelled opaque fallback, unresolved U-B2..U-B5, untested", log);
        // One upload per (file, alpha, U, V): +0x0C of id 13 and both id-4 materials read TA opaque with
        // repeat addressing, so they share it; TB is a second upload.
        Check(ReferenceEquals(t0, m4.GetTexture("_WmoTex0")) && ReferenceEquals(t0, fb4.GetTexture("_WmoTex0")) &&
              ReferenceEquals(t1, fb13.GetTexture("_WmoTex0")) && rt.Textures.Length == 2,
              "wmo 13 rows: uploads shared per (file, alpha, U, V) -- " + rt.Textures.Length + " textures", log);

        var va = new List<Vector2>();
        rt.Meshes[0].GetUVs(WmvWmoBuilder.Set2AlphaUvChannel, va);
        Check(va.Count == 4 && Near(va[0].x, 1f) && Near(va[1].x, 128f / 255f) && Near(va[2].x, 0f) && Near(va[3].x, 64f / 255f) &&
              Near(va[1].y, 0f), "wmo 13 rows: the set-2 alpha uploads as (alpha / 255, 0), RGB ignored", log);
        var none = new List<Vector2>();
        rt.Meshes[2].GetUVs(WmvWmoBuilder.Set2AlphaUvChannel, none);
        Check(none.Count == 0, "wmo 13 rows: a group without a two-layer batch gets no set-2 channel", log);
        var gap = new List<Vector2>();
        rt.Meshes[3].GetUVs(WmvWmoBuilder.Set2AlphaUvChannel, gap);
        bool ones = gap.Count == 4;
        foreach (Vector2 g in gap) ones = ones && Near(g.x, 1f);
        bool gapLogged = false;
        foreach (string line in lines) gapLogged = gapLogged || (line.Contains("group 3") && line.Contains("set-2") && line.Contains("U-V4"));
        Check(ones && gapLogged && rt.MaterialInfo[0].StreamGapVertices == 4 && rt.MaterialInfo[0].GapStreams == "MOCV set 2",
              "wmo 13 rows: a two-layer batch in a group without MOCV set 2 reads va = 1 and is logged per group and as U-V4 on its material", log);

        Check(rt.TwoLayerMaterials == 2 && rt.Set2Groups == 3 && rt.StreamGapGroups == 1 && rt.FallbackMaterials == 2 &&
              rt.ResolvedMaterials == 1 && rt.PartialMaterials == 1 && rt.UnresolvedMaterials == 2 && rt.ProvisionalMaterials == 2 &&
              rt.EmptySamplerSlots == 1 && rt.PrimaryTexturesMissing == 0 && rt.Moc2Groups == 0,
              "wmo 13 rows: counters (two-layer " + rt.TwoLayerMaterials + ", set-2 groups " + rt.Set2Groups + ", gaps " +
              rt.StreamGapGroups + ", fallbacks " + rt.FallbackMaterials + ", empty slots " + rt.EmptySamplerSlots + ")", log);
        WmvWmoMaterialInfo i0 = rt.MaterialInfo[0], i1 = rt.MaterialInfo[1];
        Check(i0.Set2Vertices == 8 && i0.Set2PartialVertices == 3 && i0.Set2ZeroVertices == 1 && i0.UnboundLayerVertices == 0 &&
              i1.Set2Vertices == 4 && i1.UnboundLayerVertices == 3,
              "wmo 13 rows: per-material set-2 exposure over drawn vertices (layer 2 shows on 3, an empty +0x18 weighted on 3)", log);
        string diag = WmvWmoBuilder.DescribeMaterialDiag(i0, m13, 2, textures);
        Check(diag.Contains("_WmoPermutation 3") && diag.Contains("t1 layer 2 (va = 0) <- +0x18 " + TB + " @ UV1") &&
              diag.Contains("on UV1") && diag.Contains("vertex colour MOCV set-2 alpha") && diag.Contains("| RESOLVED-PARTIAL: U-V4") &&
              diag.Contains("lack MOCV set 2"),
              "wmo 13 rows: the diagnostic reads back the two registers, their UV channels and the vertex stream", log);
        rt.Dispose();
    }

    /// <summary>
    /// The diffuse parts of ids 7 and 5 through the real parser, fetch policy and builder, on a synthetic WMO
    /// of four groups: g0 draws a complete id-7 material with blend 1 and an env map on +0x24, two MOTV sets
    /// and MOCV set 2; g1 an id-7 material whose +0x18 is empty, in a group whose set-2 alpha is 255 on every
    /// vertex (the U-23b fallback that weights its empty register nowhere); g2 an id-5 material with blend 1,
    /// flag 0x01 and an env map on +0x18, one MOTV set and no MOCV; g3 an id-7 material with blend 3 (the
    /// opaque fallback). Read back: decode set (no env map), permutations 5 and 6, register bindings and UV
    /// channels, unbound env registers, no alpha test anywhere, no light bypass on the emissive ids, where the
    /// set-2 channel is uploaded, upload sharing with the ids 13/4 treatment, counters, exposure counts and
    /// the diagnostic line.
    /// </summary>
    static void MapObjectEnvMetalTests(Action<string> log)
    {
        const uint TA = 980001, TB = 980002, Env = 980003;
        byte[][] records =
        {
            WmoSynthetic.Material(0, 7, 1, TA, TB, Env),      // 0 complete id 7, blend 1, env +0x24
            WmoSynthetic.Material(0, 7, 0, TB, 0, Env),       // 1 id 7 with +0x18 empty: U-23b fallback
            WmoSynthetic.Material(0x01, 5, 1, TA, Env),       // 2 id 5, blend 1, F_UNLIT, env +0x18
            WmoSynthetic.Material(0, 7, 3, TA, TB, Env),      // 3 id 7, blend 3: U-B2..U-B5 fallback
        };
        int[] groupMaterial = { 0, 1, 2, 3 };
        int[] groupUvSets = { 2, 2, 1, 2 };
        bool[] groupSet2 = { true, true, false, true };
        int n = groupMaterial.Length;
        var spec = new WmoSynthetic.RootSpec
        {
            Materials = records,
            GroupInfos = new byte[n][],
            GroupFileDataIDs = new uint[n],
            GroupNames = new string[0],
        };
        var groups = new WmoGroup[n];
        // MOCV set 2 as stored (B, G, R, A): va = 1, 128/255, 0, 64/255; the g1 copy is 255 everywhere.
        byte[] set2 = { 9, 8, 7, 255,   50, 60, 70, 128,   255, 255, 255, 0,   1, 2, 3, 64 };
        byte[] set2Full = { 0, 0, 0, 255,   0, 0, 0, 255,   0, 0, 0, 255,   0, 0, 0, 255 };
        for (int i = 0; i < n; i++)
        {
            spec.GroupInfos[i] = WmoSynthetic.GroupInfo(WmoGroupFlags.Outdoor, new WowVec3(12f * i, 0f, 0f),
                                                        new WowVec3(12f * i + 10f, 10f, 0f), 0);
            spec.GroupFileDataIDs[i] = 990000u + (uint)i;
            float x = 12f * i;
            var gs = new WmoSynthetic.GroupSpec
            {
                Flags = WmoGroupFlags.Outdoor | (groupSet2[i] ? WmoGroupFlags.ColorSet2 : 0u) |
                        (groupUvSets[i] >= 2 ? WmoGroupFlags.TwoTexCoordSets : 0u),
                Positions = new[] { x, 0f, 0f, x + 10f, 0f, 0f, x + 10f, 10f, 0f, x, 10f, 0f },
                Normals = new[] { 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f },
                Indices = new ushort[] { 0, 1, 2, 0, 2, 3 },
                Batches = new[] { WmoSynthetic.Batch(0, 6, 0, 3, WmoBatch.FlagLargeMaterialId, groupMaterial[i]) },
                Mpy2 = new byte[] { 0x20, 0, (byte)groupMaterial[i], 0, 0x20, 0, (byte)groupMaterial[i], 0 },
            };
            if (groupSet2[i])
                gs.ColorSets.Add(i == 1 ? set2Full : set2);
            for (int t = 0; t < groupUvSets[i]; t++)
                gs.TexCoordSets.Add(new[] { t, 0f, 1f + t, 0f, 1f + t, 1f, t, 1f });
            groups[i] = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(gs), "env metal group " + i, i);
        }
        WmoRoot root = WmoParser.ParseRoot(WmoSynthetic.BuildRoot(spec), "env metal root");

        var sampled = new HashSet<uint>();
        for (int i = 0; i < records.Length; i++)
            WmoMaterialSemantics.CollectSampledTextures(WmoMaterialSemantics.Plan(root.Materials[i]), sampled);
        Check(sampled.Count == 2 && sampled.Contains(TA) && sampled.Contains(TB) && !sampled.Contains(Env),
              "wmo 7/5 rows: the decode set is the two diffuse files, never the env map", log);

        var textures = new Dictionary<uint, WmvWmoTexture>
        {
            { TA, new WmvWmoTexture { FileDataID = TA, Image = FlatTexture(), Decoded = true, Width = 2, Height = 2 } },
            { TB, new WmvWmoTexture { FileDataID = TB, Image = FlatTexture(), Decoded = true, Width = 2, Height = 2 } },
            { Env, new WmvWmoTexture { FileDataID = Env, HeaderOnly = true, Width = 2, Height = 2 } },
        };
        var lines = new List<string>();
        WmvRuntimeMapObject rt = WmvWmoBuilder.Build(root, groups, textures, "WmoEnvMetal", s2 => lines.Add(s2));
        bool built = rt != null && rt.Materials.Length == 4;
        for (int i = 0; built && i < 4; i++) built = rt.Materials[i] != null;
        Check(built, "wmo 7/5 rows: built", log);
        if (!built) return;

        Material m7 = rt.Materials[0], fb7 = rt.Materials[1], m5 = rt.Materials[2], fbBlend = rt.Materials[3];
        Check(m7.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.TwoLayerEnvMetal && m7.GetFloat("_WmoUv0") == 0f &&
              m7.GetFloat("_WmoUv1") == 1f && m7.GetFloat("_AlphaTest") == 0f &&
              m7.renderQueue == (int)UnityEngine.Rendering.RenderQueue.Geometry &&
              rt.MaterialInfo[0].Verdict == "resolved-partial: U-G1,U-E2,U-E3,U-E4,U-P1",
              "wmo 7 rows: id 7 blend 1 is permutation 5, t0 on UV0 and t1 on UV1, untested, resolved-partial U-G1,U-E2,U-E3,U-E4,U-P1", log);
        var t0 = m7.GetTexture("_WmoTex0") as Texture2D;
        var t1 = m7.GetTexture("_WmoTex1") as Texture2D;
        Check(t0 != null && t1 != null && !ReferenceEquals(t0, t1) && m7.GetTexture("_WmoTex2") == null,
              "wmo 7 rows: registers 0 and 1 bound to +0x0C and +0x18, the env register t2 left unbound", log);
        Check(fb7.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.TwoLayerEnvMetal && fb7.GetTexture("_WmoTex0") != null &&
              fb7.GetTexture("_WmoTex1") == null && rt.MaterialInfo[1].Plan.ProvisionalFallback &&
              rt.MaterialInfo[1].Verdict == "unresolved: U-23b,U-G1,U-E2,U-E3,U-E4,U-P1",
              "wmo 7 rows: an empty +0x18 is the labelled fallback, its register left white", log);
        Check(m5.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.EnvMetal && m5.GetFloat("_AlphaTest") == 0f &&
              m5.GetFloat("_WmoLightBypass") == 0f && m5.GetTexture("_WmoTex1") == null &&
              rt.MaterialInfo[2].Verdict == "resolved-partial: U-G1,U-E2,U-E3,U-E4,U-P1,U-F1",
              "wmo 5 rows: id 5 blend 1 with F_UNLIT is permutation 6, never clipped, no bypass, env register unbound", log);
        Check(fbBlend.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.TwoLayerEnvMetal && fbBlend.GetFloat("_AlphaTest") == 0f &&
              fbBlend.GetFloat("_SrcBlend") == (float)UnityEngine.Rendering.BlendMode.One &&
              fbBlend.GetFloat("_DstBlend") == (float)UnityEngine.Rendering.BlendMode.Zero && fbBlend.GetFloat("_ZWrite") == 1f &&
              rt.MaterialInfo[3].Plan.ProvisionalFallback &&
              rt.MaterialInfo[3].Verdict == "unresolved: U-B2,U-B3,U-B4,U-B5,U-B7,U-G1,U-E2,U-E3,U-E4,U-P1",
              "wmo 7 rows: id 7 blend 3 is the labelled opaque fallback, untested, no factors applied (U-B7: its client row contested)", log);
        // Repeat addressing on every diffuse register: TA and TB are one upload each, shared by all four
        // materials.
        Check(ReferenceEquals(t0, m5.GetTexture("_WmoTex0")) && ReferenceEquals(t0, fbBlend.GetTexture("_WmoTex0")) &&
              ReferenceEquals(t1, fb7.GetTexture("_WmoTex0")) && ReferenceEquals(t1, fbBlend.GetTexture("_WmoTex1")) &&
              rt.Textures.Length == 2,
              "wmo 7/5 rows: uploads shared per (file, alpha, U, V) -- " + rt.Textures.Length + " textures", log);

        var va = new List<Vector2>();
        rt.Meshes[0].GetUVs(WmvWmoBuilder.Set2AlphaUvChannel, va);
        Check(va.Count == 4 && Near(va[0].x, 1f) && Near(va[1].x, 128f / 255f) && Near(va[2].x, 0f) && Near(va[3].x, 64f / 255f),
              "wmo 7 rows: the set-2 alpha uploads as (alpha / 255, 0) for the id-7 group", log);
        var none = new List<Vector2>();
        rt.Meshes[2].GetUVs(WmvWmoBuilder.Set2AlphaUvChannel, none);
        Check(none.Count == 0, "wmo 5 rows: the id-5 group gets no set-2 channel (case 5 reads no vertex stream)", log);

        Check(rt.TwoLayerMaterials == 3 && rt.EnvEmissiveOmittedMaterials == 4 && rt.Set2Groups == 3 && rt.StreamGapGroups == 0 &&
              rt.FallbackMaterials == 2 && rt.ResolvedMaterials == 0 && rt.PartialMaterials == 2 && rt.UnresolvedMaterials == 2 &&
              rt.ProvisionalMaterials == 2 && rt.EmptySamplerSlots == 1 && rt.PrimaryTexturesMissing == 0,
              "wmo 7/5 rows: counters (two-layer " + rt.TwoLayerMaterials + ", env emissive omitted " + rt.EnvEmissiveOmittedMaterials +
              ", set-2 groups " + rt.Set2Groups + ", fallbacks " + rt.FallbackMaterials + ", empty slots " + rt.EmptySamplerSlots + ")", log);
        WmvWmoMaterialInfo i0 = rt.MaterialInfo[0], i1 = rt.MaterialInfo[1];
        Check(i0.Set2Vertices == 4 && i0.Set2PartialVertices == 3 && i0.Set2ZeroVertices == 1 &&
              i1.Set2Vertices == 4 && i1.Set2PartialVertices == 0 && i1.UnboundLayerVertices == 0,
              "wmo 7 rows: per-material set-2 exposure (layer 2 shows on 3 vertices; the empty +0x18 is weighted on none)", log);
        string diag = WmvWmoBuilder.DescribeMaterialDiag(i0, m7, 1, textures);
        Check(diag.Contains("_WmoPermutation 5") && diag.Contains("t2 env map (emissive, not drawn) <- +0x24 " + Env + ", not bound") &&
              diag.Contains("t2 " + Env + " not bound") && diag.Contains("emissive c.rgb * c.a * env(t2).rgb") &&
              diag.Contains("env map +0x24 " + Env + ": not decoded, not drawn") && diag.Contains("env coordinate: VS generator cb2[1].z") &&
              diag.Contains("NOT drawn (U-G1, U-E2, U-E3, U-E4, U-P1)") && diag.Contains("| RESOLVED-PARTIAL: U-G1,U-E2,U-E3,U-E4,U-P1") &&
              diag.Contains("readback: shader 'WMV/Map Object', queue 2000"),
              "wmo 7 rows: the diagnostic reads back the permutation, the unbound env map, the emissive's equation and coordinate status, and what is not drawn", log);
        string blendDiag = WmvWmoBuilder.DescribeMaterialDiag(rt.MaterialInfo[3], fbBlend, 1, textures);
        Check(blendDiag.Contains("Src One Dst Zero SrcA One DstA Zero, ZWrite on") && blendDiag.Contains("queue 2000 Opaque") &&
              blendDiag.Contains(" | client blend: " + WmoMaterialSemantics.ClientBlendNote(3)) &&
              blendDiag.Contains("readback: shader 'WMV/Map Object', queue 2000") && blendDiag.Contains("_SrcBlend 1, _DstBlend 0"),
              "wmo 7 rows: a blend-3 diagnostic line holds the realised factors, the read-back state and the client row it does not apply", log);
        bool summary = false;
        foreach (string line in lines) summary = summary || (line.Contains("wmo built") && line.Contains("4 drawn without the env emissive"));
        Check(summary, "wmo 7/5 rows: the build summary counts the materials drawn without their env emissive", log);
        rt.Dispose();
    }

    /// <summary>
    /// U-F3 and the per-batch diagnostic line through the real parser and builder, on a synthetic WMO of four
    /// groups: g0, an interior group (MOGP 0x2000), draws an id-4 material with F_UNLIT (its light bypass
    /// applies); g1, interior too, draws an id-5 material with F_UNLIT (no bypass: its emissive keeps it out);
    /// g2, exterior, draws an id-0 material with F_UNLIT; g3 carries 0x40 beside 0x2000 and draws a second id-0
    /// material with F_UNLIT. A rule that called a group interior only when it has neither 0x8 nor 0x40 would
    /// read g3 as exterior, so that material's U-F3 shows that interior is decided by 0x2000 alone. Read back:
    /// the interior batch counts, the U-F3 verdict only where the bypass is drawn in an interior group, the
    /// bypass itself unchanged on the created materials, the counters, the material diagnostic and the per-batch
    /// line with its range, submesh, queue and depth write.
    /// </summary>
    static void MapObjectUnlitInteriorTests(Action<string> log)
    {
        const uint TA = 987001, Env = 987002;
        byte[][] records =
        {
            WmoSynthetic.Material(0x01, 4, 0, TA),          // 0 id 4, F_UNLIT: bypass, in an interior group
            WmoSynthetic.Material(0x01, 5, 0, TA, Env),     // 1 id 5, F_UNLIT: no bypass, in an interior group
            WmoSynthetic.Material(0x01, 0, 0, TA),          // 2 id 0, F_UNLIT: bypass, in an exterior group
            WmoSynthetic.Material(0x01, 0, 0, TA),          // 3 id 0, F_UNLIT: bypass, in an interior group that also has 0x40
        };
        // Retail files hold 0x2000 groups that also carry 0x40, the second bit a group-type rule may read as exterior.
        const uint InteriorWith40 = WmoGroupFlags.Indoor | 0x40u;
        uint[] groupFlags = { WmoGroupFlags.Indoor, WmoGroupFlags.Indoor, WmoGroupFlags.Outdoor, InteriorWith40 };
        ushort[][] batchCounts = { null, new ushort[] { 0, 1, 0 }, new ushort[] { 1, 0, 0 }, null };
        int n = records.Length;
        var spec = new WmoSynthetic.RootSpec
        {
            Materials = records,
            GroupInfos = new byte[n][],
            GroupFileDataIDs = new uint[n],
            GroupNames = new string[0],
        };
        var groups = new WmoGroup[n];
        for (int i = 0; i < n; i++)
        {
            spec.GroupInfos[i] = WmoSynthetic.GroupInfo(groupFlags[i], new WowVec3(12f * i, 0f, 0f), new WowVec3(12f * i + 10f, 10f, 0f), 0);
            spec.GroupFileDataIDs[i] = 988000u + (uint)i;
            float x = 12f * i;
            var gs = new WmoSynthetic.GroupSpec
            {
                Flags = groupFlags[i],
                Positions = new[] { x, 0f, 0f, x + 10f, 0f, 0f, x + 10f, 10f, 0f, x, 10f, 0f },
                Normals = new[] { 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f },
                Indices = new ushort[] { 0, 1, 2, 0, 2, 3 },
                Batches = new[] { WmoSynthetic.Batch(0, 6, 0, 3, WmoBatch.FlagLargeMaterialId, i) },
                BatchCounts = batchCounts[i],
                Mpy2 = new byte[] { 0x20, 0, (byte)i, 0, 0x20, 0, (byte)i, 0 },
            };
            gs.TexCoordSets.Add(new[] { 0f, 0f, 1f, 0f, 1f, 1f, 0f, 1f });
            groups[i] = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(gs), "unlit group " + i, i);
        }
        WmoRoot root = WmoParser.ParseRoot(WmoSynthetic.BuildRoot(spec), "unlit root");
        var textures = new Dictionary<uint, WmvWmoTexture>
        {
            { TA, new WmvWmoTexture { FileDataID = TA, Image = FlatTexture(), Decoded = true, Width = 2, Height = 2 } },
            { Env, new WmvWmoTexture { FileDataID = Env, HeaderOnly = true, Width = 2, Height = 2 } },
        };
        var lines = new List<string>();
        WmvRuntimeMapObject rt = WmvWmoBuilder.Build(root, groups, textures, "WmoUnlit", s2 => lines.Add(s2));
        bool built = rt != null && rt.Materials.Length == 4;
        for (int i = 0; built && i < 4; i++) built = rt.Materials[i] != null;
        Check(built && (groups[0].Header.Flags & WmoGroupFlags.Indoor) != 0 && (groups[2].Header.Flags & WmoGroupFlags.Indoor) == 0 &&
              (groups[3].Header.Flags & InteriorWith40) == InteriorWith40,
              "wmo U-F3 rows: built, g0/g1 interior, g2 exterior and g3 interior with 0x40 by the MOGP flags", log);
        if (!built) return;

        WmvWmoMaterialInfo i0 = rt.MaterialInfo[0], i1 = rt.MaterialInfo[1], i2 = rt.MaterialInfo[2], i3 = rt.MaterialInfo[3];
        Check(i0.InteriorBatches == 1 && i1.InteriorBatches == 1 && i2.InteriorBatches == 0 && i3.InteriorBatches == 1,
              "wmo U-F3 rows: batches are counted per material by their group's interior flag (0x40 beside 0x2000 still interior)", log);
        Check(i0.Verdict == "resolved-partial: U-F3" && i0.Plan.LightBypass && rt.Materials[0].GetFloat("_WmoLightBypass") == 1f &&
              i0.Why.Contains("F_UNLIT bypass kept on 1 drawn batch(es) in interior groups (MOGP 0x2000)"),
              "wmo U-F3 rows: an id-4 F_UNLIT material drawn in an interior group keeps its bypass and is resolved-partial U-F3", log);
        Check(i1.Verdict == "resolved-partial: U-G1,U-E2,U-E3,U-E4,U-P1,U-F1" && !i1.Plan.LightBypass &&
              rt.Materials[1].GetFloat("_WmoLightBypass") == 0f,
              "wmo U-F3 rows: an id-5 F_UNLIT material in an interior group has no bypass, so no U-F3", log);
        Check(i2.Verdict == "resolved" && rt.Materials[2].GetFloat("_WmoLightBypass") == 1f,
              "wmo U-F3 rows: an id-0 F_UNLIT material in an exterior group stays resolved, bypassed", log);
        Check(i3.Verdict == "resolved-partial: U-F3" && i3.Plan.LightBypass && rt.Materials[3].GetFloat("_WmoLightBypass") == 1f &&
              i3.Why.Contains("F_UNLIT bypass kept on 1 drawn batch(es) in interior groups (MOGP 0x2000)"),
              "wmo U-F3 rows: an id-0 F_UNLIT material drawn only in a group with 0x40 beside 0x2000 is interior by 0x2000 alone: " +
              "resolved-partial U-F3, bypass kept", log);
        Check(rt.ResolvedMaterials == 1 && rt.PartialMaterials == 3 && rt.UnresolvedMaterials == 0,
              "wmo U-F3 rows: counters take the U-F3 verdict (resolved " + rt.ResolvedMaterials + ", partial " + rt.PartialMaterials + ")", log);
        bool materialLine = false;
        foreach (string line in lines) materialLine = materialLine || (line.StartsWith("wmo material 0:") && line.Contains("(U-F3)"));
        string diag = WmvWmoBuilder.DescribeMaterialDiag(i0, rt.Materials[0], 1, textures);
        Check(materialLine && diag.Contains("| batches 1 (1 in interior groups, MOGP 0x2000)") && diag.Contains("RESOLVED-PARTIAL: U-F3") &&
              diag.Contains("light bypass on"),
              "wmo U-F3 rows: the material line and the diagnostic carry the code and the interior batch count", log);

        string b0 = WmvWmoBuilder.DescribeBatchDiag(rt, groups[0], 0, rt.SubmeshBatches[0][0], 0, rt.SubmeshMaterialIds[0][0]);
        string b1 = WmvWmoBuilder.DescribeBatchDiag(rt, groups[1], 1, rt.SubmeshBatches[1][0], 0, rt.SubmeshMaterialIds[1][0]);
        string b2 = WmvWmoBuilder.DescribeBatchDiag(rt, groups[2], 2, rt.SubmeshBatches[2][0], 0, rt.SubmeshMaterialIds[2][0]);
        string b3 = WmvWmoBuilder.DescribeBatchDiag(rt, groups[3], 3, rt.SubmeshBatches[3][0], 0, rt.SubmeshMaterialIds[3][0]);
        string past = WmvWmoBuilder.DescribeBatchDiag(rt, groups[2], 2, 5, 3, 7);
        Check(b0 == "wmo batch g0.b0 range C -> submesh 0 -> material 0 (shader 4, blend 0, queue 2000, ZWrite on, resolved-partial: U-F3)" &&
              b1 == "wmo batch g1.b0 range B -> submesh 0 -> material 1 (shader 5, blend 0, queue 2000, ZWrite on, " +
                    "resolved-partial: U-G1,U-E2,U-E3,U-E4,U-P1,U-F1)" &&
              b2 == "wmo batch g2.b0 range A -> submesh 0 -> material 2 (shader 0, blend 0, queue 2000, ZWrite on, resolved)" &&
              b3 == "wmo batch g3.b0 range C -> submesh 0 -> material 3 (shader 0, blend 0, queue 2000, ZWrite on, resolved-partial: U-F3)" &&
              past == "wmo batch g2.b5 range ? -> submesh 3 -> material 7 (past MOMT: the plain white fallback, queue ?, ZWrite ?)",
              "wmo U-F3 rows: the per-batch line names the range, submesh and material, and reads queue and depth write back (" + b0 + ")", log);
        rt.Dispose();
    }

    /// <summary>
    /// The pixel arithmetic of permutations 2 (four-layer, client case 23), 3 (two-layer, client case 13), 5 and
    /// 6 (the diffuse parts of cases 7 and 5) and of the envmask view (8) as the GPU runs it, against the C#
    /// reference transcriptions the parser tests pin (WmoMaterialSemantics.FourLayerWeights / TwoLayerMix /
    /// EnvMetalMask / TwoLayerEnvMask / FourLayerEnvMask). Without it a lerp flipped in WmvWmo.shader, a
    /// component dropped from the four-layer maximum, or a wrong alpha in an emissive mask would pass every
    /// other check.
    ///
    /// A synthetic WMO of five flat quads: a four-layer material with flat-colour layers of known alpha, height
    /// maps of known alpha, the same MOC2 bytes on every vertex and an env map on +0x0C; a two-layer material with
    /// the same MOCV set-2 alpha on every vertex; a plain id-0 reference; an id-5 material whose +0x0C has an
    /// alpha below 255, with an env map on +0x18; and an id-7 material with two layers of different alphas, the
    /// same set-2 alpha and an env map on +0x24. Every env map is a decoded image the builder could bind -- and
    /// must not. Constant vertex streams mean interpolation cannot move the sample. Each quad is drawn unlit
    /// through a diagnostic view (3 effective weights, 4 va, 5 the combiner diffuse, 8 the emissive mask) by an
    /// orthographic camera of this test's own into a render texture, and its centre pixel is compared with the
    /// REFERENCE quad drawn through view 5 with a flat texture holding the value the C# function predicts.
    /// Comparing against a reference drawn by the same shader, camera and target, rather than against the bare
    /// number, cancels whatever display transform, target encoding or readback conversion the frame passes
    /// through: both pixels take the same path.
    /// </summary>
    static void MapObjectShaderRenderTests(Action<string> log)
    {
        if (WmvWmoBuilder.ResolveMapObjectShader(null) == null)
        {
            Check(false, "wmo render: the world-model shader is missing, nothing to render", log);
            return;
        }
        if (!WmvModelBuilder.AuthoredTextureDomain)
        {
            // The C# mix is taken over the stored bytes; a player sampling textures sRGB-decoded
            // (WMV_DISPLAY) mixes decoded values, so the comparison would not be the same arithmetic.
            log("wmo render: skipped -- textures are sampled sRGB-decoded in this run, the byte-domain mix does not apply");
            return;
        }
        const uint L1 = 985001, L2 = 985002, L3 = 985003, L4 = 985004, H1 = 985005, H2 = 985006, H3 = 985007, H4 = 985008;
        const uint TA = 985011, TB = 985012, TR = 985013;
        const uint Env23 = 985021, T5 = 985022, Env5 = 985023, T7A = 985024, T7B = 985025, Env7 = 985026;
        byte[][] records =
        {
            WmoSynthetic.Material(0, 23, 0, Env23, L1, L2, new uint[] { L3, L4, H1, H2, H3, H4 }),   // 0 four-layer, env on +0x0C
            WmoSynthetic.Material(0x04, 13, 0, TA, TB),                                           // 1 two-layer
            WmoSynthetic.Material(0x04, 0, 0, TR),                                                // 2 reference
            WmoSynthetic.Material(0x04, 5, 0, T5, Env5),                                          // 3 env metal, env on +0x18
            WmoSynthetic.Material(0x04, 7, 0, T7A, T7B, Env7),                                    // 4 two-layer env metal, env on +0x24
        };
        // MOC2 as stored (B, G, R, A): layer weights 40, 80, 100 of 255 for layers 1..3 (layer 4 takes 35).
        // Layer 3 carries the largest weighted height, so a maximum that skipped it would move every weight.
        const byte W1 = 40, W2 = 80, W3 = 100, Va = 90;
        byte[] moc2 = new byte[16], set2 = new byte[16];
        for (int v = 0; v < 4; v++)
        {
            moc2[v * 4] = W3; moc2[v * 4 + 1] = W2; moc2[v * 4 + 2] = W1; moc2[v * 4 + 3] = 0;
            set2[v * 4] = 11; set2[v * 4 + 1] = 22; set2[v * 4 + 2] = 33; set2[v * 4 + 3] = Va;
        }
        int n = records.Length;
        var spec = new WmoSynthetic.RootSpec
        {
            Materials = records,
            GroupInfos = new byte[n][],
            GroupFileDataIDs = new uint[n],
            GroupNames = new string[0],
        };
        var groups = new WmoGroup[n];
        for (int i = 0; i < n; i++)
        {
            spec.GroupInfos[i] = WmoSynthetic.GroupInfo(WmoGroupFlags.Outdoor, new WowVec3(12f * i, 0f, 0f),
                                                        new WowVec3(12f * i + 10f, 10f, 0f), 0);
            spec.GroupFileDataIDs[i] = 986000u + (uint)i;
            float x = 12f * i;
            bool twoLayers = i == 1 || i == 4;
            var gs = new WmoSynthetic.GroupSpec
            {
                Flags = WmoGroupFlags.Outdoor | (twoLayers ? WmoGroupFlags.ColorSet2 | WmoGroupFlags.TwoTexCoordSets : 0u),
                Positions = new[] { x, 0f, 0f, x + 10f, 0f, 0f, x + 10f, 10f, 0f, x, 10f, 0f },
                Normals = new[] { 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f },
                Indices = new ushort[] { 0, 1, 2, 0, 2, 3 },
                Batches = new[] { WmoSynthetic.Batch(0, 6, 0, 3, WmoBatch.FlagLargeMaterialId, i) },
                Mpy2 = new byte[] { 0x20, 0, (byte)i, 0, 0x20, 0, (byte)i, 0 },
                Moc2 = i == 0 ? moc2 : null,
            };
            if (twoLayers)
                gs.ColorSets.Add(set2);
            int sets = i == 0 ? 4 : twoLayers ? 2 : 1;
            for (int s = 0; s < sets; s++)
                gs.TexCoordSets.Add(new[] { 0f, 0f, 1f, 0f, 1f, 1f, 0f, 1f });
            groups[i] = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(gs), "render group " + i, i);
        }
        WmoRoot root = WmoParser.ParseRoot(WmoSynthetic.BuildRoot(spec), "render root");

        // Layer colours, layer alphas and height alphas. Layer 4 is yellow, so every channel of the mix mixes two
        // layers; the layer alphas differ, so the emissive mask's mix.a is a real weighted sum.
        byte[][] layerRgb = { new byte[] { 255, 0, 0 }, new byte[] { 0, 255, 0 }, new byte[] { 0, 0, 255 }, new byte[] { 255, 255, 0 } };
        byte[] layerAlpha = { 180, 90, 250, 30 };
        byte[] heightAlpha = { 255, 128, 255, 200 };
        byte[] l1 = { 230, 40, 90 }, l2 = { 20, 200, 160 };
        // Id 5's +0x0C (alpha below 255) and id 7's two layers (different alphas, so a mask reading one layer's
        // alpha for both cannot match).
        byte[] t5 = { 200, 120, 60, 100 }, l7a = { 230, 40, 90, 200 }, l7b = { 20, 200, 160, 60 };
        var textures = new Dictionary<uint, WmvWmoTexture>();
        uint[] layerIds = { L1, L2, L3, L4 }, heightIds = { H1, H2, H3, H4 };
        for (int k = 0; k < 4; k++)
        {
            AddRenderTexture(textures, layerIds[k], SolidTextureAlpha(layerRgb[k][0], layerRgb[k][1], layerRgb[k][2], layerAlpha[k]));
            AddRenderTexture(textures, heightIds[k], SolidTextureAlpha(90, 90, 90, heightAlpha[k]));
        }
        AddRenderTexture(textures, TA, SolidTexture(l1[0], l1[1], l1[2]));
        AddRenderTexture(textures, TB, SolidTexture(l2[0], l2[1], l2[2]));
        AddRenderTexture(textures, TR, SolidTexture(0, 0, 0));
        AddRenderTexture(textures, T5, SolidTextureAlpha(t5[0], t5[1], t5[2], t5[3]));
        AddRenderTexture(textures, T7A, SolidTextureAlpha(l7a[0], l7a[1], l7a[2], l7a[3]));
        AddRenderTexture(textures, T7B, SolidTextureAlpha(l7b[0], l7b[1], l7b[2], l7b[3]));
        // The env maps decode like any texture: were an env binding ever readable, the builder would bind them.
        AddRenderTexture(textures, Env23, SolidTexture(255, 255, 255));
        AddRenderTexture(textures, Env5, SolidTexture(255, 255, 255));
        AddRenderTexture(textures, Env7, SolidTexture(255, 255, 255));

        // What the C# transcriptions predict, as the bytes of a flat reference texture.
        var heights = new float[4];
        for (int k = 0; k < 4; k++) heights[k] = heightAlpha[k] / 255f;
        float[] b = WmoMaterialSemantics.FourLayerWeights(W1 / 255f, W2 / 255f, W3 / 255f, heights, new[] { 1f, 1f, 1f, 1f });
        var mix = new float[3];
        for (int c = 0; c < 3; c++)
            for (int k = 0; k < 4; k++)
                mix[c] += b[k] * layerRgb[k][c] / 255f;
        float va = WmoMaterialSemantics.SetTwoAlpha(Va);
        var twoMix = new float[3];
        for (int c = 0; c < 3; c++)
            twoMix[c] = WmoMaterialSemantics.TwoLayerMix(l1[c] / 255f, l2[c] / 255f, va);
        // The emissive masks: the layers' rgba weighted exactly as the diffuse is (alpha included) for id 23, t0 for
        // id 5, the rgba lerp of the two layers for id 7 -- and id 7's diffuse, which the alpha lerp must not touch.
        var mixRgba = new float[4];
        for (int c = 0; c < 4; c++)
            for (int k = 0; k < 4; k++)
                mixRgba[c] += b[k] * (c < 3 ? layerRgb[k][c] : layerAlpha[k]) / 255f;
        float[] mask23 = WmoMaterialSemantics.FourLayerEnvMask(mixRgba);
        float[] mask5 = WmoMaterialSemantics.EnvMetalMask(t5[0] / 255f, t5[1] / 255f, t5[2] / 255f, t5[3] / 255f);
        var layer7a = new float[4];
        var layer7b = new float[4];
        for (int c = 0; c < 4; c++)
        {
            layer7a[c] = l7a[c] / 255f;
            layer7b[c] = l7b[c] / 255f;
        }
        float[] mask7 = WmoMaterialSemantics.TwoLayerEnvMask(layer7a, layer7b, va);
        var diffuse7 = new float[3];
        for (int c = 0; c < 3; c++)
            diffuse7[c] = WmoMaterialSemantics.TwoLayerMix(layer7a[c], layer7b[c], va);

        WmvRuntimeMapObject rt = null;
        GameObject camGo = null;
        RenderTexture target = null;
        RenderTexture prevActive = RenderTexture.active;
        var refs = new List<Texture2D>();
        try
        {
            rt = WmvWmoBuilder.Build(root, groups, textures, "WmoRender", null);
            bool built = rt != null && rt.Materials.Length == 5;
            for (int i = 0; built && i < 5; i++) built = rt.Materials[i] != null;
            Check(built, "wmo render: built", log);
            if (!built) return;

            // Normal rendering is untouched by the envmask view: whatever view this run has, the env maps of ids 5,
            // 7 and 23 are listed unread by their plans and never bound, while their diffuse registers are.
            Material env5 = rt.Materials[3], env7 = rt.Materials[4];
            int runView = WmvModelBuilder.Debug_.WmoView;
            string runViewName = runView > 0 ? "-wmvWmoView=" + WmvModelBuilder.Debug_.WmoViewName(runView) : "no view switch";
            Check(WmvModelBuilder.Debug_.WmoViewName(WmvWmoBuilder.EnvMaskView) == "envmask" &&
                  env5.GetTexture("_WmoTex1") == null && env7.GetTexture("_WmoTex2") == null && rt.Materials[0].GetTexture("_WmoTex0") == null &&
                  env5.GetTexture("_WmoTex0") != null && env7.GetTexture("_WmoTex0") != null && env7.GetTexture("_WmoTex1") != null &&
                  rt.Materials[0].GetTexture("_WmoTex1") != null && rt.Textures.Length == 14 &&
                  env5.GetFloat("_WmoDiagView") == (float)runView && env7.GetFloat("_WmoDiagView") == (float)runView,
                  "wmo render: the env maps of ids 5 (_WmoTex1), 7 (_WmoTex2) and 23 (_WmoTex0) are never bound, their diffuse " +
                  "registers are, 14 uploads and no env map among them (" + rt.Textures.Length + " uploads; " + runViewName + ")", log);
            // Far from anything the viewport can see, and switched off again before this method returns:
            // Destroy is deferred to the end of the frame, and the viewport camera must never draw these.
            Vector3 origin = new Vector3(-20000f, 20000f, -20000f);
            rt.Root.transform.position = origin;
            rt.Root.SetActive(true);
            foreach (Material m in rt.Materials)
                m.SetFloat("_Cull", (float)UnityEngine.Rendering.CullMode.Off);

            camGo = new GameObject("WmvWmoRenderTestCamera");
            Camera cam = camGo.AddComponent<Camera>();
            cam.enabled = false;
            cam.clearFlags = CameraClearFlags.SolidColor;
            cam.backgroundColor = new Color(0.5f, 0f, 0.5f, 1f);
            cam.orthographic = true;
            cam.orthographicSize = 1f;          // the quads are 10 units across: every pixel is inside one
            cam.nearClipPlane = 0.1f;
            cam.farClipPlane = 20f;
            cam.allowHDR = false;
            cam.allowMSAA = false;
            const int Size = 16;
            target = RenderTexture.GetTemporary(Size, Size, 24, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);

            Material four = rt.Materials[0], two = rt.Materials[1], reference = rt.Materials[2];
            // The reference: a flat texture of the predicted value, drawn by the diffuse permutation's view 5.
            Func<float, float, float, Color32> refPixel = (r, g, bl) =>
            {
                Texture2D t = WmvModelBuilder.CreateTexture(SolidTexture(ToByte(r), ToByte(g), ToByte(bl)), "WmoRenderRef", true, true);
                refs.Add(t);
                reference.SetTexture("_WmoTex0", t);
                reference.SetFloat("_WmoDiagView", 5f);
                return RenderQuadCentre(cam, target, origin, 2, Size);
            };
            Func<Material, int, float, Color32> quadPixel = (m, group, view) =>
            {
                m.SetFloat("_WmoDiagView", view);
                return RenderQuadCentre(cam, target, origin, group, Size);
            };

            Color32 black = refPixel(0f, 0f, 0f), white = refPixel(1f, 1f, 1f);
            bool rendering = white.r > black.r + 100 && white.g > black.g + 100 && white.b > black.b + 100;
            Check(rendering, "wmo render: the test camera draws the reference quad (black " + Px(black) + ", white " + Px(white) + ")", log);
            if (!rendering) return;

            Color32 weights = quadPixel(four, 0, 3f), weightsRef = refPixel(b[0], b[1], b[2]);
            Check(SamePixel(weights, weightsRef, 3),
                  "wmo render: four-layer effective weights match FourLayerWeights (" + Px(weights) + " vs " + Px(weightsRef) + ")", log);
            Color32 fourDiffuse = quadPixel(four, 0, 5f), fourRef = refPixel(mix[0], mix[1], mix[2]);
            Check(SamePixel(fourDiffuse, fourRef, 3),
                  "wmo render: four-layer diffuse matches the C# weighted layer sum (" + Px(fourDiffuse) + " vs " + Px(fourRef) + ")", log);
            Color32 vaPixel = quadPixel(two, 1, 4f), vaRef = refPixel(va, va, va);
            Check(SamePixel(vaPixel, vaRef, 3),
                  "wmo render: the two-layer factor is the stored set-2 alpha / 255 (" + Px(vaPixel) + " vs " + Px(vaRef) + ")", log);
            Color32 twoDiffuse = quadPixel(two, 1, 5f), twoRef = refPixel(twoMix[0], twoMix[1], twoMix[2]);
            Check(SamePixel(twoDiffuse, twoRef, 3),
                  "wmo render: two-layer diffuse matches TwoLayerMix, va 1 toward +0x0C (" + Px(twoDiffuse) + " vs " + Px(twoRef) + ")", log);

            // The envmask view against the C# masks: id 5 t0.rgb * t0.a, id 7 c.rgb * c.a with both layer alphas
            // lerped, id 23 mix.rgb * mix.a; id 13, whose case has no emissive, draws the view's dark grey.
            float envView = WmvWmoBuilder.EnvMaskView;
            Color32 mask5Pixel = quadPixel(env5, 3, envView), mask5Ref = refPixel(mask5[0], mask5[1], mask5[2]);
            Check(SamePixel(mask5Pixel, mask5Ref, 3),
                  "wmo render: the id-5 envmask view matches EnvMetalMask, t0.rgb * t0.a (" + Px(mask5Pixel) + " vs " + Px(mask5Ref) + ")", log);
            Color32 mask7Pixel = quadPixel(env7, 4, envView), mask7Ref = refPixel(mask7[0], mask7[1], mask7[2]);
            Check(SamePixel(mask7Pixel, mask7Ref, 3),
                  "wmo render: the id-7 envmask view matches TwoLayerEnvMask, c.rgb * c.a with both layer alphas lerped by va (" +
                  Px(mask7Pixel) + " vs " + Px(mask7Ref) + ")", log);
            Color32 mask23Pixel = quadPixel(four, 0, envView), mask23Ref = refPixel(mask23[0], mask23[1], mask23[2]);
            Check(SamePixel(mask23Pixel, mask23Ref, 3),
                  "wmo render: the id-23 envmask view matches FourLayerEnvMask of the weighted layer rgba (" + Px(mask23Pixel) + " vs " +
                  Px(mask23Ref) + ")", log);
            Color32 mask13Pixel = quadPixel(two, 1, envView), naRef = refPixel(0.1f, 0.1f, 0.1f);
            Check(SamePixel(mask13Pixel, naRef, 3),
                  "wmo render: the envmask view draws id 13 (no emissive) as not applicable (" + Px(mask13Pixel) + " vs " + Px(naRef) + ")", log);
            // Id 7's diffuse is still the two-layer lerp of rgb: the rgba lerp exists only in the view.
            Color32 diffuse7Pixel = quadPixel(env7, 4, 5f), diffuse7Ref = refPixel(diffuse7[0], diffuse7[1], diffuse7[2]);
            Check(SamePixel(diffuse7Pixel, diffuse7Ref, 3),
                  "wmo render: the id-7 diffuse (view 5) is still TwoLayerMix, the alpha lerp leaks into nothing drawn (" +
                  Px(diffuse7Pixel) + " vs " + Px(diffuse7Ref) + ")", log);
            // Id 5's diffuse is still t0.rgb: the +0x0C alpha (100 here) the mask reads reaches nothing drawn.
            Color32 diffuse5Pixel = quadPixel(env5, 3, 5f), diffuse5Ref = refPixel(t5[0] / 255f, t5[1] / 255f, t5[2] / 255f);
            Check(SamePixel(diffuse5Pixel, diffuse5Ref, 3),
                  "wmo render: the id-5 diffuse (view 5) is still t0.rgb, its alpha reaches nothing drawn (" +
                  Px(diffuse5Pixel) + " vs " + Px(diffuse5Ref) + ")", log);
            Check(env5.GetTexture("_WmoTex1") == null && env7.GetTexture("_WmoTex2") == null && rt.Materials[0].GetTexture("_WmoTex0") == null,
                  "wmo render: drawing the envmask view bound no env map", log);
        }
        finally
        {
            RenderTexture.active = prevActive;
            if (target != null) RenderTexture.ReleaseTemporary(target);
            if (camGo != null) UnityEngine.Object.DestroyImmediate(camGo);
            foreach (Texture2D t in refs) UnityEngine.Object.DestroyImmediate(t);
            if (rt != null)
            {
                if (rt.Root != null) rt.Root.SetActive(false);
                rt.Dispose();
            }
        }
    }

    static void AddRenderTexture(Dictionary<uint, WmvWmoTexture> into, uint id, BlpImage img)
    {
        into[id] = new WmvWmoTexture { FileDataID = id, Image = img, Decoded = true, Width = img.Width, Height = img.Height };
    }

    /// <summary>SolidTexture with a chosen alpha: a height map's only input is its alpha.</summary>
    static BlpImage SolidTextureAlpha(byte r, byte g, byte b, byte a)
    {
        BlpImage img = SolidTexture(r, g, b);
        for (int i = 3; i < img.Rgba.Length; i += 4) img.Rgba[i] = a;
        return img;
    }

    static byte ToByte(float v)
    {
        return (byte)Mathf.Clamp(Mathf.RoundToInt(v * 255f), 0, 255);
    }

    static bool SamePixel(Color32 a, Color32 b, int tolerance)
    {
        return Math.Abs(a.r - b.r) <= tolerance && Math.Abs(a.g - b.g) <= tolerance && Math.Abs(a.b - b.b) <= tolerance;
    }

    static string Px(Color32 c)
    {
        return c.r + "," + c.g + "," + c.b;
    }

    /// <summary>
    /// Render the test camera straight down onto synthetic group quad `group` (WoW x 12*group..+10, y 0..10,
    /// which the converter puts at Unity x -10..0, z 12*group..+10) and read back the centre pixel.
    /// </summary>
    static Color32 RenderQuadCentre(Camera cam, RenderTexture target, Vector3 origin, int group, int size)
    {
        cam.transform.position = origin + new Vector3(-5f, 5f, 12f * group + 5f);
        cam.transform.rotation = Quaternion.Euler(90f, 0f, 0f);
        cam.targetTexture = target;
        cam.Render();
        cam.targetTexture = null;
        RenderTexture prev = RenderTexture.active;
        RenderTexture.active = target;
        var tex = new Texture2D(size, size, TextureFormat.RGBA32, false, true);
        tex.ReadPixels(new Rect(0, 0, size, size), 0, 0);
        tex.Apply(false);
        RenderTexture.active = prev;
        Color32 c = tex.GetPixels32()[(size / 2) * size + size / 2];
        UnityEngine.Object.DestroyImmediate(tex);
        return c;
    }

    static WmoMaterial MaterialFrom(byte[] record, int index)
    {
        WmoRoot r = WmoParser.ParseRoot(WmoSynthetic.BuildRoot(new WmoSynthetic.RootSpec
        {
            Materials = new[] { record },
            GroupInfos = new[] { WmoSynthetic.GroupInfo(WmoGroupFlags.Outdoor, new WowVec3(0f, 0f, 0f), new WowVec3(1f, 1f, 1f), 0) },
            GroupFileDataIDs = new uint[] { 1 },
            GroupNames = new string[0],
        }), "verdict root");
        WmoMaterial m = r.Materials[0];
        m.Index = index;
        return m;
    }

    // ---------------------------------------------------------------- the terrain tile (protocol 12, stage 03)

    const int TerrainTestTileX = 32, TerrainTestTileY = 50;
    const uint TerrainTestRoot = 778037, TerrainTestRootNorth = 778038, TerrainTestRootSouth = 778039;
    const int TerrainTestWdt = 775971;

    /// <summary>
    /// The terrain tile (stage 03 design 5.7): the mesh builder on synthetic roots (AdtSynthetic) inside the player's own
    /// runtime, the Unity objects with the production layer and masks in a coverage render, holes as background in a
    /// coverage render, the edge pass's depth nudge from below (a flat tile) and from above (a wall), the session's whole
    /// load lifecycle against fake host answers (exactly one report per load, the exact reasons, the tiles' tallies,
    /// nothing left behind), its request lifetime (one request pending at most, a bounded ring of ended loads' requests,
    /// late answers dropped, nothing growing over 1200 failing loads), the camera's round trip through the terrain view,
    /// and the protocol-12 lines as the IPC client reads, writes and routes them (map answers to the terrain session only). Every Unity object is released with DestroyImmediate, so the Mesh and Material counts can
    /// be compared within this frame. (TerrainHiddenModelTests, run after these, goes through the player's own deferred
    /// disposal.)
    /// </summary>
    static void TerrainTests(Action<string> log)
    {
        int meshesBefore = Resources.FindObjectsOfTypeAll<Mesh>().Length;
        int materialsBefore = Resources.FindObjectsOfTypeAll<Material>().Length;
        int texturesBefore = Resources.FindObjectsOfTypeAll<Texture2D>().Length;
        int arraysBefore = Resources.FindObjectsOfTypeAll<Texture2DArray>().Length;
        int liveBefore = WmvRuntimeTerrainTile.Live;
        TerrainBuilderTests(log);
        TerrainCoverageTests(log);
        TerrainHoleRenderTests(log);
        TerrainEdgeTests(log);
        TerrainEdgeOccluderTests(log);
        TerrainSessionTests(log);
        TerrainRequestLifetimeTests(log);
        TerrainCameraTests(log);
        TerrainIpcTests(log);
        TerrainMapRoutingTests(log);
        TerrainMaterialTests(log);
        TerrainMapsViewerTests(log);
        TerrainWorldModelTests(log);
        TerrainDoodadTests(log);
        int meshesAfter = Resources.FindObjectsOfTypeAll<Mesh>().Length;
        int materialsAfter = Resources.FindObjectsOfTypeAll<Material>().Length;
        int texturesAfter = Resources.FindObjectsOfTypeAll<Texture2D>().Length;
        int arraysAfter = Resources.FindObjectsOfTypeAll<Texture2DArray>().Length;
        Check(WmvRuntimeTerrainTile.Live == liveBefore && meshesAfter == meshesBefore && materialsAfter == materialsBefore &&
              texturesAfter == texturesBefore && arraysAfter == arraysBefore && WmvRuntimeTerrainTile.LiveTextures == 0 &&
              WmvRuntimeTerrainTile.LiveTextureBytes == 0 && WmvRuntimeTerrainTile.LiveAlphaArrays == 0,
              string.Format("terrain: every tile, Mesh, Material, Texture2D and Texture2DArray these tests made is gone (live tiles {0} -> {1}, " +
                            "meshes {2} -> {3}, materials {4} -> {5}, textures {6} -> {7}, alpha arrays {8} -> {9}; terrain texture tallies " +
                            "{10}/{11}/{12})", liveBefore, WmvRuntimeTerrainTile.Live, meshesBefore, meshesAfter, materialsBefore,
                            materialsAfter, texturesBefore, texturesAfter, arraysBefore, arraysAfter, WmvRuntimeTerrainTile.LiveTextures,
                            WmvRuntimeTerrainTile.LiveTextureBytes, WmvRuntimeTerrainTile.LiveAlphaArrays), log);
    }

    static TerrainTileMesh TerrainFromRoot(byte[] root, int tileX, int tileY)
    {
        return TerrainMeshBuilder.Build(AdtParser.ParseRoot(root, tileX, tileY), tileX, tileY, WorldToUnity.TileOrigin(tileX, tileY));
    }

    static bool SameIndices(ushort[] a, List<ushort> b)
    {
        if (a == null || a.Length != b.Count) return false;
        for (int i = 0; i < a.Length; i++)
            if (a[i] != b[i]) return false;
        return true;
    }

    static bool SameBits(float a, float b) { return BitConverter.SingleToInt32Bits(a) == BitConverter.SingleToInt32Bits(b); }

    /// <summary>The mesh builder, run inside the player (Mono) on synthetic roots: counts, the exact triangles each single
    /// hole bit removes, a full hole mask, the ramp orientation of design 2.3c, and chunk-border copies bit-equal.</summary>
    static void TerrainBuilderTests(Action<string> log)
    {
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        TerrainTileMesh flat = TerrainFromRoot(AdtSynthetic.FlatRoot(tx, ty), tx, ty);
        Check(flat.ChunksBuilt == 256 && flat.ChunksSkipped == 0 && flat.Vertices == 37120 && flat.Triangles == 65536 && flat.HoleCells == 0 &&
              flat.MeshBytes == 1284096,
              string.Format("terrain builder: a flat tile is 256 chunks, 37,120 vertices, 65,536 triangles, 1,284,096 mesh bytes ({0}, {1}, {2}, {3})",
                            flat.ChunksBuilt, flat.Vertices, flat.Triangles, flat.MeshBytes), log);
        Check(flat.Chunks[0].Positions[0] == 0f && flat.Chunks[0].Positions[2] == 0f && flat.Chunks[0].Positions[1] == 50f,
              "terrain builder: the tile's north-west vertex is the origin, at its height (" + flat.Chunks[0].Positions[0] + ", " +
              flat.Chunks[0].Positions[1] + ", " + flat.Chunks[0].Positions[2] + ")", log);

        // Chunk n < 64 has hole bit n, chunk 64 every bit, the rest none.
        TerrainTileMesh holed = TerrainFromRoot(AdtSynthetic.RootWith(tx, ty, n => new RootChunkSpec
        {
            Holes = n < 64 ? 1UL << n : n == 64 ? ulong.MaxValue : 0UL,
        }), tx, ty);
        var cell = new int[12];
        int exact = 0;
        for (int b = 0; b < 64; b++)
        {
            var want = new List<ushort>(756);
            for (int k = 0; k < 64; k++)
            {
                if (k == b) continue;
                TerrainMeshBuilder.CellIndices(k / 8, k % 8, cell);
                foreach (int i in cell) want.Add((ushort)i);
            }
            TerrainChunkMesh c = holed.Chunks[b];
            if (c.State == TerrainChunkState.Built && c.Triangles == 252 && c.HoleCells == 1 && c.HoleMask == 1UL << b && SameIndices(c.Indices, want))
                exact++;
        }
        Check(exact == 64, "terrain builder: each of the 64 single hole bits removes exactly its own cell's four triangles (" + exact + " of 64)", log);
        TerrainChunkMesh full = holed.Chunks[64];
        Check(full.State == TerrainChunkState.Built && full.Triangles == 0 && full.Indices.Length == 0 && full.HoleCells == 64 &&
              full.BoundsMin == null && holed.Triangles == 65536 - 64 * 4 - 256 && holed.HoleCells == 128 && holed.Vertices == 255 * 145,
              string.Format("terrain builder: a full mask leaves a built chunk with no triangle and no bounds; the tile has {0} triangles, " +
                            "{1} hole cells, {2} vertices", holed.Triangles, holed.HoleCells, holed.Vertices), log);

        // Design 2.3c: a height rising toward world +x rises toward Unity +Z; rising toward world -y rises toward Unity +X.
        TerrainTileMesh north = TerrainFromRoot(AdtSynthetic.RampRoot(tx, ty, 0.1, 0.0), tx, ty);
        TerrainTileMesh west = TerrainFromRoot(AdtSynthetic.RampRoot(tx, ty, 0.0, -0.1), tx, ty);
        double worstNorth = 0, worstWest = 0;
        bool upward = true;
        for (int n = 0; n < 256; n++)
            for (int k = 0; k < 145; k++)
            {
                float[] p = north.Chunks[n].Positions, q = west.Chunks[n].Positions;
                worstNorth = Math.Max(worstNorth, Math.Abs(p[3 * k + 1] - (100.0 + 0.1 * p[3 * k + 2])));
                worstWest = Math.Max(worstWest, Math.Abs(q[3 * k + 1] - (100.0 + 0.1 * q[3 * k])));
                upward &= north.Chunks[n].Normals[3 * k + 1] > 0f && west.Chunks[n].Normals[3 * k + 1] > 0f;
            }
        float southEnd = north.Chunks[255].Positions[3 * 144 + 1], northEnd = north.Chunks[0].Positions[1];
        float westEnd = west.Chunks[0].Positions[1], eastEnd = west.Chunks[15].Positions[3 * 8 + 1];
        Check(worstNorth < 1e-3 && worstWest < 1e-3 && northEnd > southEnd && eastEnd > westEnd && upward,
              string.Format(System.Globalization.CultureInfo.InvariantCulture,
                            "terrain builder: ramps (2.3c) -- rising toward world +x rises toward Unity +Z (y - 0.1 z off by {0:G3}; north edge " +
                            "{1}, south {2}), rising toward world -y rises toward Unity +X (y - 0.1 x off by {3:G3}; east edge {4}, west {5}); " +
                            "every normal points up", worstNorth, northEnd, southEnd, worstWest, eastEnd, westEnd), log);

        // A curved field, so the normals differ from vertex to vertex: the copies of a border vertex are bit-equal.
        TerrainTileMesh hills = TerrainFromRoot(AdtSynthetic.RootWith(tx, ty, n => new RootChunkSpec
        {
            BaseHeight = 100f,
            Height = AdtSynthetic.WorldHeights(tx, ty, n, (x, y) => 100.0 + 7.0 * Math.Sin(x / 31.0) * Math.Cos(y / 17.0), 100f),
        }), tx, ty);
        int pairs = 0, unequal = 0;
        var distinct = new HashSet<int>();
        for (int iy = 0; iy < 16; iy++)
            for (int ix = 0; ix < 16; ix++)
            {
                TerrainChunkMesh a = hills.Chunks[iy * 16 + ix];
                for (int k = 0; k < 145; k++)
                    distinct.Add(BitConverter.SingleToInt32Bits(a.Normals[3 * k]) ^ BitConverter.SingleToInt32Bits(a.Normals[3 * k + 2]) * 31);
                for (int r = 0; r <= 8; r++)
                {
                    if (ix < 15)
                    {
                        TerrainChunkMesh e = hills.Chunks[iy * 16 + ix + 1];
                        unequal += BorderDiffers(a, TerrainVertexLayout.Outer(r, 8), e, TerrainVertexLayout.Outer(r, 0));
                        pairs++;
                    }
                    if (iy < 15)
                    {
                        TerrainChunkMesh s = hills.Chunks[(iy + 1) * 16 + ix];
                        unequal += BorderDiffers(a, TerrainVertexLayout.Outer(8, r), s, TerrainVertexLayout.Outer(0, r));
                        pairs++;
                    }
                }
            }
        Check(pairs == 4320 && unequal == 0 && distinct.Count > 1000,
              "terrain builder: chunk-border copies of a vertex are bit-equal in position and normal (" + (pairs - unequal) + " of " + pairs +
              " shared points; " + distinct.Count + " distinct normals in the field)", log);
    }

    static int BorderDiffers(TerrainChunkMesh a, int ka, TerrainChunkMesh b, int kb)
    {
        for (int i = 0; i < 3; i++)
            if (!SameBits(a.Positions[3 * ka + i], b.Positions[3 * kb + i]) || !SameBits(a.Normals[3 * ka + i], b.Normals[3 * kb + i]))
                return 1;
        return 0;
    }

    /// <summary>Render `cam` into `target` over black and over white; the pixels equal in both are covered by geometry
    /// (the light check's rule). Returns the count, and the frame over black in `overBlack` when asked.</summary>
    static int TerrainCovered(Camera cam, RenderTexture target, int size)
    {
        int covered = 0;
        foreach (bool c in TerrainCoverageMap(cam, target, size))
            if (c) covered++;
        return covered;
    }

    /// <summary>The same rule pixel by pixel: true where the pixel is equal over black and over white. Rows run bottom
    /// up (Texture2D.GetPixels32).</summary>
    static bool[] TerrainCoverageMap(Camera cam, RenderTexture target, int size)
    {
        Color32[] black = TerrainGrab(cam, target, size, Color.black);
        Color32[] white = TerrainGrab(cam, target, size, Color.white);
        var map = new bool[black.Length];
        for (int i = 0; i < black.Length; i++)
            map[i] = black[i].r == white[i].r && black[i].g == white[i].g && black[i].b == white[i].b;
        return map;
    }

    static Color32[] TerrainGrab(Camera cam, RenderTexture target, int size, Color clear)
    {
        cam.backgroundColor = clear;
        cam.targetTexture = target;
        cam.Render();
        cam.targetTexture = null;
        RenderTexture prev = RenderTexture.active;
        RenderTexture.active = target;
        var tex = new Texture2D(size, size, TextureFormat.RGBA32, false, true);
        tex.ReadPixels(new Rect(0, 0, size, size), 0, 0);
        tex.Apply(false);
        RenderTexture.active = prev;
        Color32[] px = tex.GetPixels32();
        UnityEngine.Object.DestroyImmediate(tex);
        return px;
    }

    static Camera TerrainTestCamera(out GameObject go)
    {
        go = new GameObject("WmvTerrainTestCamera");
        Camera cam = go.AddComponent<Camera>();
        cam.enabled = false;                 // rendered by hand only
        cam.clearFlags = CameraClearFlags.SolidColor;
        cam.allowHDR = false;
        cam.allowMSAA = false;
        cam.aspect = 1f;
        return cam;
    }

    /// <summary>The Unity objects and the production layer and masks: every GameObject and Renderer on layer 30, the
    /// mesh format, built inactive; a camera with TerrainViewMask covers the whole tile from above and nothing from below
    /// (back faces culled, no edge meshes), and ModelViewMask -- the model view's -- shows none of it.</summary>
    static void TerrainCoverageTests(Action<string> log)
    {
        Check(WmvTerrainBuilder.ResolveShader(null) != null, "terrain render: Resources/WmvTerrain.shader ships in the player", log);
        if (WmvTerrainBuilder.ResolveShader(null) == null)
            return;
        Check(WmvTerrainBuilder.TerrainViewMask == 1 << 30 && WmvTerrainBuilder.ModelViewMask == ~(1 << 30) &&
              WmvTerrainBuilder.WithoutTerrain(WmvTerrainBuilder.TerrainViewMask) == 0 &&
              WmvTerrainBuilder.WithoutTerrain(-1) == WmvTerrainBuilder.ModelViewMask,
              "terrain render: the two fixed masks (terrain view 1 << 30, model view everything else) and the rig's mask without terrain", log);
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        TerrainTileMesh mesh = TerrainFromRoot(AdtSynthetic.RampRoot(tx, ty, 0.05, -0.03), tx, ty);
        var rt = new WmvRuntimeTerrainTile(0, tx, ty, 1);
        GameObject camGo = null;
        RenderTexture target = null;
        RenderTexture prevActive = RenderTexture.active;
        try
        {
            WmvTerrainBuilder.Build(rt, mesh, null, new WmvTerrainBuildOptions { View = WmvTerrainView.Shaded, OnlyChunk = -1 }, null);
            Check(rt.Root != null && !rt.Root.activeSelf, "terrain render: the tile is built inactive", log);
            bool layers = true;
            int objects = 0, renderers = 0;
            foreach (Transform t in rt.Root.GetComponentsInChildren<Transform>(true))
            {
                objects++;
                layers &= t.gameObject.layer == WmvTerrainBuilder.Layer;
            }
            foreach (Renderer r in rt.Root.GetComponentsInChildren<Renderer>(true))
            {
                renderers++;
                layers &= r.gameObject.layer == WmvTerrainBuilder.Layer;
            }
            Check(layers && objects == 257 && renderers == 256 && rt.MeshCount == 256 && rt.MaterialCount == 1 && rt.GameObjectCount == 257,
                  string.Format("terrain render: every GameObject ({0}) and Renderer ({1}) under the root is on layer 30; 256 meshes, 1 material " +
                                "({2}, {3})", objects, renderers, rt.MeshCount, rt.MaterialCount), log);
            bool format = true;
            double boundsOff = 0;
            for (int n = 0; n < 256; n++)
            {
                Mesh m = rt.ChunkMeshes[n];
                format &= m != null && m.subMeshCount == 1 && m.GetTopology(0) == MeshTopology.Triangles &&
                          m.indexFormat == UnityEngine.Rendering.IndexFormat.UInt16 && m.vertexCount == 145 &&
                          rt.ChunkObjects[n].transform.localToWorldMatrix == Matrix4x4.identity;
                if (m == null) continue;
                Bounds b = m.bounds;
                TerrainChunkMesh c = mesh.Chunks[n];
                for (int a = 0; a < 3; a++)
                    boundsOff = Math.Max(boundsOff, Math.Max(Math.Abs(b.min[a] - c.BoundsMin[a]), Math.Abs(b.max[a] - c.BoundsMax[a])));
            }
            Check(format && boundsOff <= 1e-4 && rt.Root.transform.localToWorldMatrix == Matrix4x4.identity,
                  "terrain render: one sub-mesh, Triangles, UInt16, 145 vertices per chunk, identity transforms, mesh bounds = the builder's " +
                  "(within " + boundsOff.ToString("G3") + " yd)", log);

            rt.Root.SetActive(true);
            Camera cam = TerrainTestCamera(out camGo);
            const int Size = 64;
            target = RenderTexture.GetTemporary(Size, Size, 24, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
            float half = (float)(TerrainCoordinates.TileSize * 0.5);
            cam.orthographic = true;
            cam.orthographicSize = half;
            cam.nearClipPlane = 1f;
            cam.farClipPlane = 400f;
            cam.cullingMask = WmvTerrainBuilder.TerrainViewMask;
            cam.transform.SetPositionAndRotation(new Vector3(half, 200f, -half), Quaternion.Euler(90f, 0f, 0f));
            int top = TerrainCovered(cam, target, Size);
            cam.transform.SetPositionAndRotation(new Vector3(half, 0f, -half), Quaternion.Euler(-90f, 0f, 0f));
            int below = TerrainCovered(cam, target, Size);
            cam.cullingMask = WmvTerrainBuilder.ModelViewMask;
            cam.transform.SetPositionAndRotation(new Vector3(half, 200f, -half), Quaternion.Euler(90f, 0f, 0f));
            int modelView = TerrainCovered(cam, target, Size);
            Check(top == Size * Size && below == 0 && modelView == 0,
                  string.Format("terrain render: with the terrain view's mask the tile covers {0} of {1} pixels from above and {2} from below " +
                                "(front faces up, back faces culled); the model view's mask shows {3}", top, Size * Size, below, modelView), log);
        }
        finally
        {
            RenderTexture.active = prevActive;
            if (target != null) RenderTexture.ReleaseTemporary(target);
            if (camGo != null) UnityEngine.Object.DestroyImmediate(camGo);
            rt.DisposeImmediate();
        }
    }

    /// <summary>
    /// Holes in Unity (design 5.5), on the holed synthetic tile the builder test uses -- chunk n (0..63) with hole bit n
    /// alone, chunk 64 with every bit -- built by the real builder: the fully holed chunk gets no GameObject, no Mesh and
    /// no bounds to focus on, a single-bit chunk 252 triangles, and the object counts are exact. Two top-down coverage
    /// renders with the terrain view's mask, over two clear colours (the light check's rule), show the holes as
    /// background and nothing else: chunk 10's footprint is covered everywhere but its cell at row 1, column 2 (hole bit
    /// 10), and chunks 64, 65, 80 and 81 everywhere but chunk 64's quarter. Released with DisposeImmediate: Unity's counts
    /// and the tiles' tallies are back where they were.
    /// </summary>
    static void TerrainHoleRenderTests(Action<string> log)
    {
        if (WmvTerrainBuilder.ResolveShader(null) == null)
            return;
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        const int SingleBitChunk = 10, FullChunk = 64;
        TerrainTileMesh mesh = TerrainFromRoot(AdtSynthetic.RootWith(tx, ty, n => new RootChunkSpec
        {
            Holes = n < 64 ? 1UL << n : n == FullChunk ? ulong.MaxValue : 0UL,
        }), tx, ty);
        int meshes0 = Meshes(), materials0 = Materials(), live0 = WmvRuntimeTerrainTile.Live;
        string tallies0 = TerrainTallies();
        var rt = new WmvRuntimeTerrainTile(0, tx, ty, 3);
        GameObject camGo = null;
        RenderTexture target = null;
        RenderTexture prevActive = RenderTexture.active;
        try
        {
            WmvTerrainBuilder.Build(rt, mesh, null, new WmvTerrainBuildOptions { View = WmvTerrainView.Shaded, OnlyChunk = -1 }, null);
            Bounds unused;
            bool namedFull = false;
            int transforms = 0;
            foreach (Transform t in rt.Root.GetComponentsInChildren<Transform>(true))
            {
                transforms++;
                namedFull |= t.name.StartsWith("MCNK " + FullChunk + " ", StringComparison.Ordinal);
            }
            Mesh single = rt.ChunkMeshes[SingleBitChunk];
            int singleIndices = single != null ? (int)single.GetIndexCount(0) : -1;
            Check(rt.ChunkObjects[FullChunk] == null && rt.ChunkMeshes[FullChunk] == null && !rt.TryGetChunkBounds(FullChunk, out unused) &&
                  !namedFull && rt.TryGetChunkBounds(SingleBitChunk, out unused) && singleIndices == 252 * 3,
                  "terrain holes: the fully holed chunk " + FullChunk + " has no GameObject, no Mesh and no bounds (-wmvTerrainFocus refuses it); " +
                  "the single-bit chunk " + SingleBitChunk + " has " + singleIndices + " indices (252 triangles)", log);
            Check(tallies0 == "0/0/0" && rt.ChunkObjectCount == 255 && rt.MeshCount == 255 && rt.MaterialCount == 1 && rt.GameObjectCount == 256 &&
                  transforms == 256 && Meshes() == meshes0 + 255 && Materials() == materials0 + 1 && TerrainTallies() == "255/1/256" &&
                  WmvRuntimeTerrainTile.Live == live0 + 1,
                  string.Format("terrain holes: the objects are exact -- {0} chunk objects, {1} GameObjects under the root (the root and 255 " +
                                "chunks), Unity's Mesh count +{2} and Material count +{3}, the tiles' tallies {4} (from {5})", rt.ChunkObjectCount,
                                transforms, Meshes() - meshes0, Materials() - materials0, TerrainTallies(), tallies0), log);

            rt.Root.SetActive(true);
            Camera cam = TerrainTestCamera(out camGo);
            const int Size = 64;
            target = RenderTexture.GetTemporary(Size, Size, 24, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
            float c = (float)TerrainCoordinates.ChunkSize;
            cam.orthographic = true;
            cam.nearClipPlane = 1f;
            cam.farClipPlane = 400f;
            cam.cullingMask = WmvTerrainBuilder.TerrainViewMask;
            // Straight down, screen up = north (Unity +Z), right = east (+X); a chunk's cell row r runs south, column c east.
            // Chunk 10 (column 10, row 0) is x 10C..11C, z -C..0: 8 px per cell, its hole the cell at row 1, column 2.
            cam.orthographicSize = c * 0.5f;
            cam.transform.SetPositionAndRotation(new Vector3(10.5f * c, 200f, -0.5f * c), Quaternion.Euler(90f, 0f, 0f));
            string cellBox;
            int[] cell = HoleCoverage(TerrainCoverageMap(cam, target, Size), Size, (col, rowFromTop) => col / 8 == 2 && rowFromTop / 8 == 1, out cellBox);
            Check(cell[0] == 64 && cell[1] == 0 && cell[2] == Size * Size - 64 && cell[3] == 0,
                  string.Format("terrain holes: top down over chunk {0}, its hole cell (row 1, column 2) is background -- {1} of its 64 pixels " +
                                "uncovered ({2} covered), the other {3} of {4} covered ({5} not); uncovered pixels at {6}", SingleBitChunk, cell[0],
                                cell[1], cell[2], Size * Size - 64, cell[3], cellBox), log);
            // Chunks 64, 65 (row 4) and 80, 81 (row 5), columns 0..1: x 0..2C, z -6C..-4C, 32 px per chunk; chunk 64 the
            // north-west quarter.
            cam.orthographicSize = c;
            cam.transform.SetPositionAndRotation(new Vector3(c, 200f, -5f * c), Quaternion.Euler(90f, 0f, 0f));
            string chunkBox;
            int[] quarter = HoleCoverage(TerrainCoverageMap(cam, target, Size), Size, (col, rowFromTop) => col < 32 && rowFromTop < 32, out chunkBox);
            Check(quarter[0] == 1024 && quarter[1] == 0 && quarter[2] == Size * Size - 1024 && quarter[3] == 0,
                  string.Format("terrain holes: top down over chunks 64, 65, 80 and 81, the fully holed chunk 64 is background -- {0} of its " +
                                "1024 pixels uncovered ({1} covered), the three chunks beside it covered ({2} of {3}, {4} not); uncovered " +
                                "pixels at {5}", quarter[0], quarter[1], quarter[2], Size * Size - 1024, quarter[3], chunkBox), log);
        }
        finally
        {
            RenderTexture.active = prevActive;
            if (target != null) RenderTexture.ReleaseTemporary(target);
            if (camGo != null) UnityEngine.Object.DestroyImmediate(camGo);
            rt.DisposeImmediate();
        }
        Check(Meshes() == meshes0 && Materials() == materials0 && WmvRuntimeTerrainTile.Live == live0 && TerrainTallies() == tallies0,
              "terrain holes: DisposeImmediate takes the holed tile back to the counts before it (meshes " + (Meshes() - meshes0) + ", materials " +
              (Materials() - materials0) + " over; tallies " + TerrainTallies() + ")", log);
    }

    /// <summary>Count a coverage map against an expected hole region: [uncovered in it, covered in it, covered outside it,
    /// uncovered outside it]; `box` says where the uncovered pixels are (columns, rows from the top).</summary>
    static int[] HoleCoverage(bool[] covered, int size, Func<int, int, bool> inHole, out string box)
    {
        var n = new int[4];
        int c0 = int.MaxValue, c1 = -1, r0 = int.MaxValue, r1 = -1;
        for (int py = 0; py < size; py++)
            for (int px = 0; px < size; px++)
            {
                int rowFromTop = size - 1 - py;      // GetPixels32 rows run bottom up; screen up is north
                bool cov = covered[py * size + px];
                if (inHole(px, rowFromTop)) n[cov ? 1 : 0]++;
                else n[cov ? 2 : 3]++;
                if (!cov)
                {
                    c0 = Math.Min(c0, px); c1 = Math.Max(c1, px);
                    r0 = Math.Min(r0, rowFromTop); r1 = Math.Max(r1, rowFromTop);
                }
            }
        box = c1 < 0 ? "none" : string.Format("columns {0}..{1}, rows {2}..{3} from the top", c0, c1, r0, r1);
        return n;
    }

    /// <summary>
    /// The edge pass's depth nudge (WmvTerrainBuilder.LineDepthNudge), measured from both sides. From below: a flat tile --
    /// every triangle coplanar, the hardest case for an edge lying on its own surface -- drawn shadedwire from the default
    /// framing (the world-model view the terrain view frames with), a chunk focus and a grazing view: every pixel the
    /// edges cover alone must show over their own surfaces (a nudge too small fails here). From above: a wall of raised
    /// chunks in front of flat ones, its depth gap well beyond the nudge's documented price -- the flat chunks' edges
    /// behind it must not show through it at the production nudge, and must at 100 times it (proving the check can fail:
    /// a nudge too large fails here).
    /// </summary>
    static void TerrainEdgeTests(Action<string> log)
    {
        if (WmvTerrainBuilder.ResolveShader(null) == null)
            return;
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        TerrainTileMesh mesh = TerrainFromRoot(AdtSynthetic.FlatRoot(tx, ty), tx, ty);
        var rt = new WmvRuntimeTerrainTile(0, tx, ty, 2);
        GameObject camGo = null;
        RenderTexture target = null;
        RenderTexture prevActive = RenderTexture.active;
        try
        {
            WmvTerrainBuilder.Build(rt, mesh, null, new WmvTerrainBuildOptions { View = WmvTerrainView.ShadedWire, OnlyChunk = -1 }, null);
            Mesh edges0 = rt.LineObjects[0] != null ? rt.LineObjects[0].GetComponent<MeshFilter>().sharedMesh : null;
            // A full chunk's unique edges: 4 spokes per cell (256) and the 9 x 8 + 9 x 8 edges of the outer grid (144).
            Check(rt.LineObjectCount == 256 && rt.MaterialCount == 2 && rt.MeshCount == 512 && edges0 != null &&
                  rt.LineObjects[0].layer == WmvTerrainBuilder.Layer && edges0.GetTopology(0) == MeshTopology.Lines && edges0.GetIndexCount(0) == 2 * 400 &&
                  WmvTerrainBuilder.UniqueEdges(mesh.Chunks[0].Indices).Length == 2 * 400,
                  "terrain edges: one edge object per chunk on layer 30, Lines topology, each unique triangle edge once (400 per full chunk: " +
                  (edges0 != null ? (edges0.GetIndexCount(0) / 2).ToString() : "none") + ")", log);
            rt.Root.SetActive(true);
            Camera cam;
            WmvOrbitCamera orbit = EdgeTestCamera(out camGo, out cam);
            const int Size = 512;
            target = RenderTexture.GetTemporary(Size, Size, 24, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
            // The nudge measured over a range, in three framings: the terrain view's own two (the tile, a focused chunk; both
            // from the world-model view, 30 degrees above the horizon) and a grazing one (10 degrees). The production value
            // must show every edge pixel in all three; the table says where each framing starts to. Lines only ever change
            // the pixels they cover (they draw after the surfaces), so this bounds the nudge from below only; the occluder
            // test bounds it from above.
            float[] candidates = { 0f, 1e-5f, 2e-5f, 5e-5f, 1e-4f, 2e-4f };
            string[] framings = { "the tile framed as the terrain view frames it", "a chunk framed", "a chunk seen 10 degrees above the horizon" };
            for (int v = 0; v < framings.Length; v++)
            {
                orbit.FrameMapObject(v == 0 ? rt.Bounds : rt.ChunkBounds[136]);
                if (v == 2)
                    orbit.SetView(orbit.yaw, 10f, 1f);
                var table = new System.Text.StringBuilder();
                int edgePixels = 0, shown = 0;
                foreach (float k in candidates)
                {
                    rt.LineMaterial.SetFloat("_WmvLineNudge", k);
                    EdgeVisibility(rt, cam, target, Size, out edgePixels, out shown);
                    table.Append(table.Length > 0 ? ", " : "").Append(k.ToString("G2", System.Globalization.CultureInfo.InvariantCulture)).Append(": ")
                         .Append(shown).Append('/').Append(edgePixels);
                }
                rt.LineMaterial.SetFloat("_WmvLineNudge", WmvTerrainBuilder.LineDepthNudge);
                log("terrain edges: " + framings[v] + ", 512 px: edge pixels shown over their own surfaces by nudge -- " + table);
                EdgeVisibility(rt, cam, target, Size, out edgePixels, out shown);
                Check(edgePixels > 0 && shown == edgePixels,
                      string.Format("terrain edges: {0} -- all {1} pixels the edges cover alone show over their own surfaces ({2}) with the " +
                                    "nudge {3} of clip w (not too small)", framings[v], edgePixels, shown, WmvTerrainBuilder.LineDepthNudge), log);
            }
        }
        finally
        {
            RenderTexture.active = prevActive;
            if (target != null) RenderTexture.ReleaseTemporary(target);
            if (camGo != null) UnityEngine.Object.DestroyImmediate(camGo);
            rt.DisposeImmediate();
        }
    }

    /// <summary>
    /// The nudge from above: rows 6 and 7 of chunks raised 60 yd into a wall (flat on top), every other chunk flat, drawn
    /// shadedwire with the tile framed as the terrain view frames it and seen from the north, 30 degrees above the
    /// horizon, so the wall stands between the camera and the rows south of it. A ray over the wall meets the ground
    /// 60 / sin 30 = 120 yd further on -- well beyond the documented price of the nudge (about k * D^2 / near) at the far
    /// side of the tile, and above 5 % of the framing distance. The edges of the chunks behind the wall must not show
    /// through it at WmvTerrainBuilder.LineDepthNudge, and must at 100 times it -- this check can fail.
    /// </summary>
    static void TerrainEdgeOccluderTests(Action<string> log)
    {
        if (WmvTerrainBuilder.ResolveShader(null) == null)
            return;
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        const int WallFirstRow = 6, WallLastRow = 7;
        const float Ground = 50f, WallHeight = 60f;
        TerrainTileMesh mesh = TerrainFromRoot(AdtSynthetic.RootWith(tx, ty, n => new RootChunkSpec
        {
            BaseHeight = n / 16 >= WallFirstRow && n / 16 <= WallLastRow ? Ground + WallHeight : Ground, Height = k => 0f,
        }), tx, ty);
        var behind = new bool[AdtParser.ChunksPerTile];
        var wall = new bool[AdtParser.ChunksPerTile];
        for (int n = 0; n < AdtParser.ChunksPerTile; n++)
        {
            wall[n] = n / 16 >= WallFirstRow && n / 16 <= WallLastRow;
            behind[n] = n / 16 > WallLastRow;
        }
        var rt = new WmvRuntimeTerrainTile(0, tx, ty, 5);
        GameObject camGo = null;
        RenderTexture target = null;
        RenderTexture prevActive = RenderTexture.active;
        try
        {
            WmvTerrainBuilder.Build(rt, mesh, null, new WmvTerrainBuildOptions { View = WmvTerrainView.ShadedWire, OnlyChunk = -1 }, null);
            rt.Root.SetActive(true);
            Camera cam;
            WmvOrbitCamera orbit = EdgeTestCamera(out camGo, out cam);
            const int Size = 512;
            target = RenderTexture.GetTemporary(Size, Size, 24, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
            orbit.FrameMapObject(rt.Bounds);
            orbit.SetView(WmvOrbitCamera.FrontYaw, WmvOrbitCamera.MapObjectPitch, 1f);   // from the north (+Z), looking south

            // The documented price at the farthest edge (the middle of the tile's south side) against the wall's depth gap.
            float t = (float)TerrainCoordinates.TileSize;
            double farthest = (cam.transform.position - new Vector3(t * 0.5f, Ground, -t)).magnitude;
            double price = WmvTerrainBuilder.LineDepthNudge * farthest * farthest / cam.nearClipPlane;
            double gap = WallHeight / Math.Sin(WmvOrbitCamera.MapObjectPitch * Math.PI / 180.0);
            Check(gap >= 5.0 * price && gap >= 0.05 * orbit.distance,
                  string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                "terrain edges, occluder: (precondition) the wall's depth gap {0:F0} yd is well beyond the nudge's price at the far " +
                                "side of the tile, k D^2 / near = {1:F1} yd (D {2:F0}, near {3:F2}), and above 5 % of the framing distance {4:F0}",
                                gap, price, farthest, cam.nearClipPlane, orbit.distance), log);

            int pixels, shown, pixels100, shown100;
            OccludedEdges(rt, cam, target, Size, behind, wall, out pixels, out shown);
            rt.LineMaterial.SetFloat("_WmvLineNudge", WmvTerrainBuilder.LineDepthNudge * 100f);
            OccludedEdges(rt, cam, target, Size, behind, wall, out pixels100, out shown100);
            rt.LineMaterial.SetFloat("_WmvLineNudge", WmvTerrainBuilder.LineDepthNudge);
            Check(pixels > 0 && shown == 0,
                  string.Format("terrain edges, occluder: none of the {0} edge pixels of the chunks behind the wall that fall inside its " +
                                "footprint shows through it with the nudge {1} of clip w ({2} do)", pixels, WmvTerrainBuilder.LineDepthNudge, shown), log);
            Check(pixels100 > 0 && shown100 > 0,
                  string.Format("terrain edges, occluder: with 100 times the nudge ({0}) {1} of those {2} pixels show through the wall -- the check " +
                                "above can fail", WmvTerrainBuilder.LineDepthNudge * 100f, shown100, pixels100), log);
        }
        finally
        {
            RenderTexture.active = prevActive;
            if (target != null) RenderTexture.ReleaseTemporary(target);
            if (camGo != null) UnityEngine.Object.DestroyImmediate(camGo);
            rt.DisposeImmediate();
        }
    }

    /// <summary>The orbit camera the edge tests render with by hand: 60 degree field of view, square, no HDR or MSAA, the
    /// terrain view's mask.</summary>
    static WmvOrbitCamera EdgeTestCamera(out GameObject go, out Camera cam)
    {
        WmvOrbitCamera orbit = NewCamera(out go);
        cam = go.GetComponent<Camera>();
        cam.enabled = false;
        cam.clearFlags = CameraClearFlags.SolidColor;
        cam.allowHDR = false;
        cam.allowMSAA = false;
        cam.aspect = 1f;
        cam.cullingMask = WmvTerrainBuilder.TerrainViewMask;
        return orbit;
    }

    /// <summary>Edges alone (surface renderers off) over white: the edge pixels. Surfaces alone, and both, over white:
    /// how many edge pixels differ from the surfaces-alone frame (show).</summary>
    static void EdgeVisibility(WmvRuntimeTerrainTile rt, Camera cam, RenderTexture target, int size, out int edgePixels, out int shown)
    {
        SetRenderers(rt.ChunkObjects, false);
        SetRenderers(rt.LineObjects, true);
        Color32[] edges = TerrainGrab(cam, target, size, Color.white);
        SetRenderers(rt.LineObjects, false);
        SetRenderers(rt.ChunkObjects, true);
        Color32[] surfaces = TerrainGrab(cam, target, size, Color.white);
        SetRenderers(rt.LineObjects, true);
        Color32[] both = TerrainGrab(cam, target, size, Color.white);
        edgePixels = shown = 0;
        for (int i = 0; i < edges.Length; i++)
            if (!IsWhite(edges[i]))
            {
                edgePixels++;
                if (!SameRgb(both[i], surfaces[i])) shown++;
            }
    }

    /// <summary>The edges of the `behind` chunks alone: where they are; the `wall` chunks' surfaces alone: the wall's
    /// footprint. Every surface, then every surface and those edges: of the edge pixels inside the footprint (`pixels`),
    /// how many differ -- show through the wall (`shown`). The renderers are left as built.</summary>
    static void OccludedEdges(WmvRuntimeTerrainTile rt, Camera cam, RenderTexture target, int size, bool[] behind, bool[] wall,
                              out int pixels, out int shown)
    {
        SetRenderers(rt.ChunkObjects, false);
        SetRenderers(rt.LineObjects, false);
        SetRenderersWhere(rt.LineObjects, behind, true);
        Color32[] edges = TerrainGrab(cam, target, size, Color.white);
        SetRenderers(rt.LineObjects, false);
        SetRenderersWhere(rt.ChunkObjects, wall, true);
        Color32[] footprint = TerrainGrab(cam, target, size, Color.white);
        SetRenderers(rt.ChunkObjects, true);
        Color32[] surfaces = TerrainGrab(cam, target, size, Color.white);
        SetRenderersWhere(rt.LineObjects, behind, true);
        Color32[] both = TerrainGrab(cam, target, size, Color.white);
        SetRenderers(rt.LineObjects, true);
        pixels = shown = 0;
        for (int i = 0; i < edges.Length; i++)
            if (!IsWhite(edges[i]) && !IsWhite(footprint[i]))
            {
                pixels++;
                if (!SameRgb(both[i], surfaces[i])) shown++;
            }
    }

    static bool IsWhite(Color32 c) { return c.r == 255 && c.g == 255 && c.b == 255; }
    static bool SameRgb(Color32 a, Color32 b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

    static void SetRenderers(GameObject[] objects, bool on)
    {
        foreach (GameObject go in objects)
        {
            if (go == null) continue;
            MeshRenderer r = go.GetComponent<MeshRenderer>();
            if (r != null) r.enabled = on;
        }
    }

    static void SetRenderersWhere(GameObject[] objects, bool[] which, bool on)
    {
        for (int n = 0; n < objects.Length; n++)
        {
            if (objects[n] == null || !which[n]) continue;
            MeshRenderer r = objects[n].GetComponent<MeshRenderer>();
            if (r != null) r.enabled = on;
        }
    }

    // ---- the session against fake host answers

    /// <summary>The session's Unity side in the self-test: the real builder, DisposeImmediate, roots left inactive (nothing
    /// is ever drawn by the scene camera), the real counters; failures on demand.</summary>
    sealed class TestTerrainView : IWmvTerrainView
    {
        public bool ViewOn, FailBuild, FailShow, LastLeaveForModelLoad;
        public int Shown, Left, Disposed, BuiltHooks;
        /// <summary>The terrain tallies the moment the last tile's objects existed, before the session committed it.</summary>
        public string TalliesAtBuild = "";
        /// <summary>The view to build in; null = as the player decides it without -wmvTerrainView (textured for a textured or
        /// partial material, shaded otherwise).</summary>
        public WmvTerrainView? View;
        public bool PlainDiagnostic;
        public WmvTerrainTileContent LastContent;
        public WmvTerrainView LastView;

        public bool DrawsPlainDiagnostic { get { return PlainDiagnostic; } }

        public void BuildTile(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh, WmvTerrainTileContent content)
        {
            LastContent = content;
            LastView = View ?? (content != null && content.HasDrawableChunks ? WmvTerrainView.Textured : WmvTerrainView.Shaded);
            WmvTerrainBuilder.Build(tile, mesh, content, null, new WmvTerrainBuildOptions { View = LastView, OnlyChunk = -1 }, null);
            TalliesAtBuild = TerrainTallies();
            if (FailBuild)
                throw new InvalidOperationException("test: the build fails after its objects exist");
        }

        public void ShowTile(WmvRuntimeTerrainTile tile)
        {
            Shown++;
            ViewOn = true;
            if (FailShow)
                throw new InvalidOperationException("test: showing fails");
        }

        public void LeaveTerrainView(string why, bool forModelLoad) { Left++; ViewOn = false; LastLeaveForModelLoad = forModelLoad; }

        /// <summary>Deferred: dispose as the player does (Dispose, Object.Destroy at the end of the frame), keeping what
        /// it owned so the test can release it at once afterwards (DestroyPendingNow) and still count within this frame.</summary>
        public bool Deferred;
        readonly List<UnityEngine.Object> pendingDestroy = new List<UnityEngine.Object>();

        public void DisposeTile(WmvRuntimeTerrainTile tile)
        {
            Disposed++;
            if (!Deferred)
            {
                tile.DisposeImmediate();
                return;
            }
            pendingDestroy.AddRange(tile.OwnedObjects());
            tile.Dispose();
        }

        public void DestroyPendingNow()
        {
            foreach (UnityEngine.Object o in pendingDestroy)
                if (o != null) UnityEngine.Object.DestroyImmediate(o);
            pendingDestroy.Clear();
        }
        public void Sample(WmvIpcClient.TerrainTileReport report) { WmvTerrainBuilder.SampleCounters(report); }
        public void TileBuilt(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh, WmvTerrainTileContent content, WmvIpcClient.TerrainTileCommand command,
                              int rootFileDataID)
        {
            BuiltHooks++;
        }
    }

    /// <summary>A session with a fake host: the requests it sends, the reports it makes, a clock and frames.</summary>
    sealed class TerrainHarness
    {
        public readonly TestTerrainView View = new TestTerrainView();
        public readonly List<WmvIpcClient.TerrainTileReport> Reports = new List<WmvIpcClient.TerrainTileReport>();
        public readonly List<string[]> Sent = new List<string[]>();    // requestId, role, fileDataID, load, epoch
        public readonly WmvTerrainSession Session;
        public double Now = 1000.0;
        int next;

        public TerrainHarness(double timeoutMs = WmvTerrainSession.DefaultTimeoutMs)
        {
            Session = new WmvTerrainSession(View, (role, id, load, epoch) =>
            {
                string r = "t" + (++next);
                Sent.Add(new[] { r, role, id.ToString(), load.ToString(), epoch.ToString() });
                return r;
            }, r => Reports.Add(r), null, a => a(), timeoutMs);
        }

        public string Last(string role)
        {
            for (int i = Sent.Count - 1; i >= 0; i--)
                if (Sent[i][1] == role) return Sent[i][0];
            return null;
        }

        public string[] LastSent(string role)
        {
            for (int i = Sent.Count - 1; i >= 0; i--)
                if (Sent[i][1] == role) return Sent[i];
            return null;
        }

        public void Pump(int frames)
        {
            for (int i = 0; i < frames; i++)
                Session.Pump(Now += 16.0);
        }

        /// <summary>The host's answer to request `id`: ok with the bytes ("available", their md5 unless one is given), or
        /// refused with a state and detail. True when the session applied it (false: dropped as late or unowned).
        /// loadOverride (when > 0) is the load the answer names instead of the request's.</summary>
        public bool Answer(string id, byte[] data, string state = "available", string detail = "", string md5 = null, int loadOverride = 0)
        {
            string[] req = null;
            foreach (string[] s in Sent) if (s[0] == id) req = s;
            bool ok = state == "available";
            return Session.ClaimMapAssetResponse(new WmvIpcClient.AssetResponse
            {
                requestId = id, ok = ok, fileDataID = req != null ? int.Parse(req[2]) : 0, role = req != null ? req[1] : "",
                load = loadOverride > 0 ? loadOverride : req != null ? int.Parse(req[3]) : 0, assetEpoch = req != null ? int.Parse(req[4]) : 0, state = state,
                detail = detail, readMs = 0.5, md5 = ok ? (md5 ?? WmvTerrainSession.Md5Hex(data)) : "", data = ok ? data : null,
                byteLength = ok ? data.Length : 0, error = ok ? "" : detail, path = "",
            });
        }

        public int CountFor(int load)
        {
            int n = 0;
            foreach (var r in Reports) if (r.Load == load) n++;
            return n;
        }

        public WmvIpcClient.TerrainTileReport For(int load)
        {
            foreach (var r in Reports) if (r.Load == load) return r;
            return null;
        }
    }

    static WmvIpcClient.TerrainTileCommand TerrainCommand(int load, int tileX, int tileY, int maxBytes = 4194304, int wdt = TerrainTestWdt)
    {
        return new WmvIpcClient.TerrainTileCommand
        {
            Load = load, AssetEpoch = 7, MapId = 0, MapDirectory = "Azeroth", Product = "test", Build = "0", WdtFileDataID = wdt,
            WdtResolvedBy = "fileDataID", TileX = tileX, TileY = tileY, MaxAssetBytes = maxBytes,
        };
    }

    /// <summary>A WDT whose MAIN flags the given tiles (x, y pairs) with MAID word 0 = the paired root id (0 = none).</summary>
    static byte[] TerrainWdt(bool withMaid, params int[] tileXYRoot)
    {
        var main = new List<int>();
        var maid = new byte[4096 * 32];
        for (int i = 0; i + 2 < tileXYRoot.Length; i += 3)
        {
            int idx = tileXYRoot[i + 1] * 64 + tileXYRoot[i];
            main.Add(idx);
            AdtSynthetic.PutU32(maid, idx * 32, (uint)tileXYRoot[i + 2]);
        }
        return withMaid ? AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Mphd(0), AdtSynthetic.MainTable(main.ToArray()), AdtSynthetic.Chunk("MAID", maid))
                        : AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Mphd(0), AdtSynthetic.MainTable(main.ToArray()));
    }

    /// <summary>Load, both answers, three frames: the report of a load that builds.</summary>
    static WmvIpcClient.TerrainTileReport TerrainBuild(TerrainHarness h, int load, int tileX, int tileY, byte[] wdt, byte[] root)
    {
        h.Session.Load(TerrainCommand(load, tileX, tileY));
        h.Answer(h.Last(WmvTerrainSession.RoleWdt), wdt);
        h.Answer(h.Last(WmvTerrainSession.RoleRootAdt), root);
        h.Pump(3);
        return h.For(load);
    }

    static int Meshes() { return Resources.FindObjectsOfTypeAll<Mesh>().Length; }
    static int Materials() { return Resources.FindObjectsOfTypeAll<Material>().Length; }

    /// <summary>runtimeState's terrainMeshes / terrainMaterials / terrainGameObjects, as "meshes/materials/gameObjects".</summary>
    static string TerrainTallies()
    {
        return WmvRuntimeTerrainTile.LiveMeshes + "/" + WmvRuntimeTerrainTile.LiveMaterials + "/" + WmvRuntimeTerrainTile.LiveGameObjects;
    }

    /// <summary>The tallies of one shaded tile with every chunk built: 256 meshes, 1 material, the root and 256 chunk objects.</summary>
    const string OneShadedTile = "256/1/257";

    /// <summary>The message of the exception `a` throws, or null when it throws none.</summary>
    static string ThrownMessage(Action a)
    {
        try { a(); return null; }
        catch (Exception e) { return e.Message; }
    }

    /// <summary>prefix + message, or null (no reason can match) when there is no message.</summary>
    static string Prefixed(string prefix, string message) { return message == null ? null : prefix + message; }

    static void TerrainSessionTests(Action<string> log)
    {
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        byte[] wdt = TerrainWdt(true, tx, ty, (int)TerrainTestRoot, tx, ty - 1, (int)TerrainTestRootNorth, tx, ty + 1, 0);
        byte[] root = AdtSynthetic.FlatRoot(tx, ty);
        byte[] rootNorth = AdtSynthetic.FlatRoot(tx, ty - 1);

        // A world model on screen beside the terrain tests: nothing they do may touch it.
        const uint wmoFirstGroup = 960000, wmoTexture = 961000;
        WmoRoot wmoRoot = WmoParser.ParseRoot(WmoSynthetic.SquaresRoot(3, wmoFirstGroup, wmoTexture), "terrain-test squares root");
        var wmoGroups = new WmoGroup[3];
        for (int i = 0; i < 3; i++)
            wmoGroups[i] = WmoParser.ParseGroup(WmoSynthetic.SquaresGroup(i), "terrain-test squares group " + i, i);
        var wmoTextures = new Dictionary<uint, WmvWmoTexture>
        {
            { wmoTexture, new WmvWmoTexture { FileDataID = wmoTexture, Image = FlatTexture(), Decoded = true, Width = 2, Height = 2 } },
        };
        WmvRuntimeMapObject wmo = WmvWmoBuilder.Build(wmoRoot, wmoGroups, wmoTextures, "TerrainTestWmo", null);
        int wmoLive = WmvRuntimeMapObject.Live, modelLive = WmvRuntimeModel.Live;

        int meshes0 = Meshes(), materials0 = Materials(), live0 = WmvRuntimeTerrainTile.Live;
        // Nothing of a tile left: Unity's own counts, the live runtimes, and the tiles' tallies runtimeState sends
        // (terrainMeshes, terrainMaterials, terrainGameObjects: 0 with no tile).
        Func<bool> clean = () => Meshes() == meshes0 && Materials() == materials0 && WmvRuntimeTerrainTile.Live == live0 &&
                                 TerrainTallies() == "0/0/0";
        Check(TerrainTallies() == "0/0/0" && live0 == 0,
              "terrain session: no tile is held before the session tests -- the tiles' tallies are 0 (" + TerrainTallies() + "), live " + live0, log);
        var loads = new List<KeyValuePair<TerrainHarness, int>>();
        var harnesses = new List<TerrainHarness>();

        // ---- a load that builds: the report waits for frames N+1 and N+2, and says what was built and served
        var h = new TerrainHarness();
        harnesses.Add(h);
        h.Session.Load(TerrainCommand(1, tx, ty));
        string[] wdtAsk = h.LastSent(WmvTerrainSession.RoleWdt);
        Check(wdtAsk != null && wdtAsk[2] == TerrainTestWdt.ToString() && wdtAsk[3] == "1" && wdtAsk[4] == "7" && h.Session.Loading,
              "terrain session: a load asks for the WDT by the command's FileDataID, load and epoch", log);
        h.Answer(h.Last(WmvTerrainSession.RoleWdt), wdt);
        string[] rootAsk = h.LastSent(WmvTerrainSession.RoleRootAdt);
        Check(rootAsk != null && rootAsk[2] == TerrainTestRoot.ToString() && rootAsk[3] == "1",
              "terrain session: then the root ADT MAID word 0 names for the tile (" + (rootAsk != null ? rootAsk[2] : "none") + ")", log);
        h.Answer(h.Last(WmvTerrainSession.RoleRootAdt), root);
        h.Pump(1);
        bool adoptedQuiet = h.Reports.Count == 0 && h.View.Shown == 1 && WmvRuntimeTerrainTile.Live == live0 + 1;
        h.Pump(1);
        bool secondQuiet = h.Reports.Count == 0;
        h.Pump(1);
        WmvIpcClient.TerrainTileReport built = h.For(1);
        Check(adoptedQuiet && secondQuiet && built != null && built.Status == "built",
              "terrain session: adopted in frame N, reported \"built\" only once frames N+1 and N+2 have started", log);
        if (built != null)
            Check(built.HasMesh && built.ChunksBuilt == 256 && built.Vertices == 37120 && built.Triangles == 65536 && built.HoleCells == 0 &&
                  built.HasBounds && built.WdtFileDataID == TerrainTestWdt && built.RootFileDataID == (int)TerrainTestRoot &&
                  built.WdtBytes == wdt.Length && built.RootBytes == root.Length && built.WdtMd5 == WmvTerrainSession.Md5Hex(wdt) &&
                  built.RootMd5 == WmvTerrainSession.Md5Hex(root) && built.LiveTerrainTiles == live0 + 1 && built.FirstFrameMs >= 0 &&
                  built.ParseMs >= 0 && built.BuildMs >= 0 && built.UploadMs >= 0 && built.MeshObjects == meshes0 + 256 &&
                  built.MaterialObjects == materials0 + 1 && built.OriginX == (32 - ty) * TerrainCoordinates.TileSize &&
                  built.RootOffset == Vector3.zero && h.View.BuiltHooks == 1 && h.Session.ShownLoad == 1 && h.Session.ViewActive &&
                  !h.Session.Loading,
                  string.Format("terrain session: the built report carries the mesh, the files' sizes and md5s, the origin and the counters " +
                                "(meshes {0} = {1} + 256, materials {2}, live {3})", built.MeshObjects, meshes0, built.MaterialObjects,
                                built.LiveTerrainTiles), log);
        Check(TerrainTallies() == OneShadedTile && h.View.TalliesAtBuild == OneShadedTile && built != null && built.TerrainGameObjects == 257,
              "terrain session: while the tile is shown the tiles' tallies hold its own objects -- " + TerrainTallies() + " (meshes/materials/" +
              "game objects; the scene root never counted), already at its build before the commit (" + h.View.TalliesAtBuild + "); the report's " +
              "terrainGameObjects " + (built != null ? built.TerrainGameObjects.ToString() : "none"), log);

        // ---- replace: another tile, then the same tile again -- one tile alive each time
        WmvIpcClient.TerrainTileReport second = TerrainBuild(h, 2, tx, ty - 1, wdt, rootNorth);
        bool replaced = second != null && second.Status == "built" && WmvRuntimeTerrainTile.Live == live0 + 1 && h.View.Disposed == 1 &&
                        h.Session.ShownLoad == 2 && h.Session.SceneTiles == 1 && Meshes() == meshes0 + 256;
        WmvIpcClient.TerrainTileReport again = TerrainBuild(h, 3, tx, ty - 1, wdt, rootNorth);
        Check(replaced && again != null && again.Status == "built" && WmvRuntimeTerrainTile.Live == live0 + 1 && h.View.Disposed == 2 &&
              Meshes() == meshes0 + 256 && Materials() == materials0 + 1,
              "terrain session: a new tile replaces the shown one, and the same tile again replaces it too -- one tile alive (" +
              (WmvRuntimeTerrainTile.Live - live0) + "), one tile's meshes", log);

        // ---- clear after built: the tile goes, the view is left, no report
        int reportsBefore = h.Reports.Count;
        h.Session.Clear("test");
        Check(h.Reports.Count == reportsBefore && clean() && h.View.Left == 1 && !h.View.ViewOn && !h.View.LastLeaveForModelLoad &&
              !h.Session.ViewActive && h.Session.ShownLoad == 0,
              "terrain session: clearTerrain after the build takes the tile away and leaves the view (not for a model load), without a " +
              "report; the tiles' tallies are back to " + TerrainTallies(), log);
        loads.Add(new KeyValuePair<TerrainHarness, int>(h, 1));
        loads.Add(new KeyValuePair<TerrainHarness, int>(h, 2));
        loads.Add(new KeyValuePair<TerrainHarness, int>(h, 3));

        // ---- every host state a request can be refused with: exactly one "failed" each, the state passed on, nothing left
        string[] states = { "stale", "not in client storage", "not installed locally", "unreadable", "unsupported", "refused" };
        int load = 10;
        foreach (bool onRoot in new[] { false, true })
            foreach (string state in states)
            {
                var hs = new TerrainHarness();
                harnesses.Add(hs);
                hs.Session.Load(TerrainCommand(load, tx, ty));
                string detail = "detail of " + state;
                if (onRoot)
                {
                    hs.Answer(hs.Last(WmvTerrainSession.RoleWdt), wdt);
                    hs.Answer(hs.Last(WmvTerrainSession.RoleRootAdt), null, state, detail);
                }
                else
                    hs.Answer(hs.Last(WmvTerrainSession.RoleWdt), null, state, detail);
                hs.Pump(3);
                WmvIpcClient.TerrainTileReport r = hs.For(load);
                string want = string.Format("the host answered the {0} request (FileDataID {1}) \"{2}\": {3}", onRoot ? "root ADT" : "WDT",
                                            onRoot ? (int)TerrainTestRoot : TerrainTestWdt, state, detail);
                Check(hs.CountFor(load) == 1 && r.Status == "failed" && r.HostState == state && r.HostDetail == detail &&
                      r.Reason == want && clean() && !hs.Session.Loading && !hs.View.ViewOn,
                      "terrain session: the " + (onRoot ? "root ADT" : "WDT") + " answered \"" + state + "\" fails the load once, with the host's " +
                      "state and detail, reason exactly \"" + want + "\" (" + (r != null ? r.Reason : "no report") + ")", log);
                loads.Add(new KeyValuePair<TerrainHarness, int>(hs, load));
                load++;
            }

        // ---- the other failures: each one "failed" with EXACTLY its reason (no host state), nothing left behind. A reason
        // another part words (a parser's exception, the mesh builder's) is taken from that part on the same bytes, so the
        // session's own prefix -- or the lack of one -- is what is checked.
        string wdtWhat = string.Format("wdt {0} (map {1})", TerrainTestWdt, 0);
        string rootWhat = string.Format("root ADT {0} ({1}_{2})", TerrainTestRoot, tx, ty);
        Action<string, int, Action<TerrainHarness>, string> fails = (what, cmdLoad, drive, reason) =>
        {
            var hf = new TerrainHarness();
            harnesses.Add(hf);
            drive(hf);
            hf.Pump(3);
            WmvIpcClient.TerrainTileReport r = hf.For(cmdLoad);
            bool exact = r != null && reason != null && r.Reason == reason;
            Check(hf.CountFor(cmdLoad) == 1 && r.Status == "failed" && exact && r.HostState == "" && r.HostDetail == "" && clean() &&
                  !hf.Session.Loading,
                  "terrain session: " + what + " -> one \"failed\", reason exactly \"" + reason + "\"" +
                  (exact ? "" : " (got \"" + (r != null ? r.Reason : "no report") + "\")") + ", no host state, nothing left", log);
            loads.Add(new KeyValuePair<TerrainHarness, int>(hf, cmdLoad));
        };
        fails("a WDT answer larger than maxAssetBytes", 30, x =>
        {
            x.Session.Load(TerrainCommand(30, tx, ty, 1000));
            x.Answer(x.Last(WmvTerrainSession.RoleWdt), wdt);
        }, "answer of " + wdt.Length + " bytes exceeds maxAssetBytes 1000 (WDT, FileDataID " + TerrainTestWdt + ")");
        byte[] shortRoot = AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.ShortMcnk());
        fails("a malformed root", 31, x =>
        {
            x.Session.Load(TerrainCommand(31, tx, ty));
            x.Answer(x.Last(WmvTerrainSession.RoleWdt), wdt);
            x.Answer(x.Last(WmvTerrainSession.RoleRootAdt), shortRoot);
        }, Prefixed("root ADT parse failed: ", ThrownMessage(() => AdtParser.ParseRoot(shortRoot, tx, ty, rootWhat))));
        fails("a tile whose MAIN flag is clear", 32, x =>
        {
            x.Session.Load(TerrainCommand(32, tx + 5, ty));
            x.Answer(x.Last(WmvTerrainSession.RoleWdt), wdt);
        }, "no tile at " + (tx + 5) + "_" + ty + " (MAIN flag clear)");
        fails("a tile MAID names no root for", 33, x =>
        {
            x.Session.Load(TerrainCommand(33, tx, ty + 1));
            x.Answer(x.Last(WmvTerrainSession.RoleWdt), wdt);
        }, "MAID names no root ADT for " + tx + "_" + (ty + 1));
        fails("a WDT without MAID", 34, x =>
        {
            x.Session.Load(TerrainCommand(34, tx, ty));
            x.Answer(x.Last(WmvTerrainSession.RoleWdt), TerrainWdt(false, tx, ty, 0));
        }, "MAID names no root ADT for " + tx + "_" + ty + " (the WDT has no MAID)");
        // The builder's own TerrainBuildException text, passed on as it is (no "terrain mesh build failed" prefix).
        string mismatch = ThrownMessage(() => TerrainMeshBuilder.Build(AdtParser.ParseRoot(rootNorth, tx, ty, rootWhat), tx, ty,
                                                                       WorldToUnity.TileOrigin(tx, ty)));
        Check(mismatch != null && mismatch.StartsWith("root ADT does not belong to tile " + tx + "_" + ty, StringComparison.Ordinal),
              "terrain session: (precondition) the builder refuses the root of another tile: " + mismatch, log);
        fails("a root of another tile", 35, x =>
        {
            x.Session.Load(TerrainCommand(35, tx, ty));
            x.Answer(x.Last(WmvTerrainSession.RoleWdt), wdt);
            x.Answer(x.Last(WmvTerrainSession.RoleRootAdt), rootNorth);
        }, mismatch);
        fails("WDT bytes that differ from the host's md5", 36, x =>
        {
            x.Session.Load(TerrainCommand(36, tx, ty));
            x.Answer(x.Last(WmvTerrainSession.RoleWdt), wdt, "available", "", "00000000000000000000000000000000");
        }, "the WDT bytes received hash to " + WmvTerrainSession.Md5Hex(wdt) + ", the host served 00000000000000000000000000000000");
        fails("a command naming no WDT", 37, x => x.Session.Load(TerrainCommand(37, tx, ty, 4194304, 0)),
              "loadTerrainTile names no WDT FileDataID (0)");
        fails("a tile outside the map", 38, x => x.Session.Load(TerrainCommand(38, 64, ty)), "tile 64_" + ty + " is outside 0..63");
        fails("a build that fails after its Unity objects exist", 39, x =>
        {
            x.View.FailBuild = true;
            x.Session.Load(TerrainCommand(39, tx, ty));
            x.Answer(x.Last(WmvTerrainSession.RoleWdt), wdt);
            x.Answer(x.Last(WmvTerrainSession.RoleRootAdt), root);
        }, "building the tile's Unity objects failed: InvalidOperationException: test: the build fails after its objects exist");
        // The root's md5 is checked apart from the WDT's, after the worker.
        fails("root ADT bytes that differ from the host's md5", 42, x =>
        {
            x.Session.Load(TerrainCommand(42, tx, ty));
            x.Answer(x.Last(WmvTerrainSession.RoleWdt), wdt);
            x.Answer(x.Last(WmvTerrainSession.RoleRootAdt), root, "available", "", "00000000000000000000000000000000");
        }, "the root ADT bytes received hash to " + WmvTerrainSession.Md5Hex(root) + ", the host served 00000000000000000000000000000000");
        Check(root.Length > wdt.Length, "terrain session: (precondition) the synthetic root (" + root.Length + " bytes) is larger than the WDT (" +
              wdt.Length + "), so a limit of the WDT's size passes the WDT and refuses the root", log);
        fails("a root ADT answer larger than maxAssetBytes", 43, x =>
        {
            x.Session.Load(TerrainCommand(43, tx, ty, wdt.Length));
            x.Answer(x.Last(WmvTerrainSession.RoleWdt), wdt);
            x.Answer(x.Last(WmvTerrainSession.RoleRootAdt), root);
        }, "answer of " + root.Length + " bytes exceeds maxAssetBytes " + wdt.Length + " (root ADT, FileDataID " + TerrainTestRoot + ")");
        // Answers the fake host never makes, fed to the session as the client would hand them over.
        Func<TerrainHarness, int, bool, int, byte[], string, string, bool> raw = (x, rawLoad, ok, fileDataID, data, state, error) =>
            x.Session.ClaimMapAssetResponse(new WmvIpcClient.AssetResponse
            {
                requestId = x.Last(WmvTerrainSession.RoleWdt), ok = ok, fileDataID = fileDataID, role = WmvTerrainSession.RoleWdt,
                load = rawLoad, assetEpoch = 7, state = state, detail = "", readMs = 0.5,
                md5 = data != null ? WmvTerrainSession.Md5Hex(data) : "", data = data, byteLength = data != null ? data.Length : 0,
                error = error, path = "",
            });
        fails("an answer naming another FileDataID", 44, x =>
        {
            x.Session.Load(TerrainCommand(44, tx, ty));
            raw(x, 44, true, 999, wdt, "available", "");
        }, "the WDT answer names FileDataID 999, the request " + TerrainTestWdt);
        fails("an answer that carries no bytes", 45, x =>
        {
            x.Session.Load(TerrainCommand(45, tx, ty));
            raw(x, 45, true, TerrainTestWdt, null, "available", "");
        }, "the WDT answer (FileDataID " + TerrainTestWdt + ") carries no bytes");
        fails("a transport failure (not ok, no host state)", 46, x =>
        {
            x.Session.Load(TerrainCommand(46, tx, ty));
            raw(x, 46, false, TerrainTestWdt, null, "", "the connection closed");
        }, "the WDT request (FileDataID " + TerrainTestWdt + ") failed: the connection closed");
        byte[] badWdt = AdtSynthetic.Cat(AdtSynthetic.Mver());
        fails("a WDT the parser refuses", 47, x =>
        {
            x.Session.Load(TerrainCommand(47, tx, ty));
            x.Answer(x.Last(WmvTerrainSession.RoleWdt), badWdt);
        }, Prefixed("WDT parse failed: ", ThrownMessage(() => WdtParser.Parse(badWdt, wdtWhat))));
        // Every MCNK shorter than its header: the root parses, no chunk decodes, the builder refuses the tile.
        byte[] undecodable = AdtSynthetic.Root(tx, ty, n => AdtSynthetic.ShortMcnk());
        string parseProblem = ThrownMessage(() => AdtParser.ParseRoot(undecodable, tx, ty, rootWhat));
        string noChunk = parseProblem != null ? null
                         : ThrownMessage(() => TerrainMeshBuilder.Build(AdtParser.ParseRoot(undecodable, tx, ty, rootWhat), tx, ty,
                                                                        WorldToUnity.TileOrigin(tx, ty)));
        Check(parseProblem == null && noChunk != null && noChunk.StartsWith("no decodable chunk", StringComparison.Ordinal),
              "terrain session: (precondition) a root of 256 short MCNKs parses, and the builder refuses it \"" + noChunk + "\"" +
              (parseProblem != null ? " -- but the parse threw: " + parseProblem : ""), log);
        fails("a root with no decodable chunk", 48, x =>
        {
            x.Session.Load(TerrainCommand(48, tx, ty));
            x.Answer(x.Last(WmvTerrainSession.RoleWdt), wdt);
            x.Answer(x.Last(WmvTerrainSession.RoleRootAdt), undecodable);
        }, noChunk);
        // A command with no load serial (the host never sends one): reported once, under load 0, nothing asked for.
        fails("a command carrying no load serial", 0, x => x.Session.Load(TerrainCommand(0, tx, ty)), "loadTerrainTile carries no load serial (0)");

        // ---- the timeout: 30 s after the first frame that saw the load, not a moment before; a late answer is dropped
        {
            var ht = new TerrainHarness();
            harnesses.Add(ht);
            ht.Session.Load(TerrainCommand(40, tx, ty));
            string waited = "timed out after 30 s waiting for the WDT answer (request " + ht.Last(WmvTerrainSession.RoleWdt) + ")";
            ht.Session.Pump(5000.0);
            ht.Session.Pump(35000.0);
            bool notYet = ht.Reports.Count == 0;
            ht.Session.Pump(35000.5);
            WmvIpcClient.TerrainTileReport r = ht.For(40);
            bool applied = ht.Answer(ht.Last(WmvTerrainSession.RoleWdt), wdt);
            ht.Pump(3);
            Check(notYet && ht.CountFor(40) == 1 && r.Status == "failed" && r.Reason == waited && !applied && ht.Session.LateAnswersDropped == 1 &&
                  ht.CountFor(40) == 1 && clean(),
                  "terrain session: a load not answered within 30 s fails once, reason exactly \"" + waited + "\" (" +
                  (r != null ? r.Reason : "no report") + "); its late answer is dropped as late", log);
            loads.Add(new KeyValuePair<TerrainHarness, int>(ht, 40));
            Check(!ht.Session.ClaimMapAssetResponse(new WmvIpcClient.AssetResponse { requestId = "u999", ok = true, data = new byte[1] }) &&
                  ht.Session.UnownedAnswersDropped == 1 && ht.CountFor(40) == 1,
                  "terrain session: an answer to a request no load of the session made is dropped as unowned", log);
        }

        // ---- supersede: a newer load answers the one in flight "superseded" once -- asking, on the worker, or adopted
        {
            var hp = new TerrainHarness();
            harnesses.Add(hp);
            hp.Session.Load(TerrainCommand(50, tx, ty));
            string firstAsk = hp.Last(WmvTerrainSession.RoleWdt);
            hp.Session.Load(TerrainCommand(51, tx, ty));
            WmvIpcClient.TerrainTileReport r50 = hp.For(50);
            bool late = !hp.Answer(firstAsk, wdt) && hp.Session.LateAnswersDropped == 1;
            hp.Answer(hp.Last(WmvTerrainSession.RoleWdt), wdt);
            hp.Answer(hp.Last(WmvTerrainSession.RoleRootAdt), root);
            // 52 superseded with its worker result queued: the result is dropped when the frame applies it.
            hp.Session.Load(TerrainCommand(52, tx, ty));
            WmvIpcClient.TerrainTileReport r51 = hp.For(51);
            string supersededNone = TerrainTallies();
            hp.Answer(hp.Last(WmvTerrainSession.RoleWdt), wdt);
            hp.Answer(hp.Last(WmvTerrainSession.RoleRootAdt), root);
            hp.Pump(1);
            bool oneAdopted = WmvRuntimeTerrainTile.Live == live0 + 1 && hp.View.Shown == 1 && TerrainTallies() == OneShadedTile;
            // 53 supersedes 52 after its adoption, before its report: 52 is "superseded", its tile stays until 53's commit.
            hp.Session.Load(TerrainCommand(53, tx, ty - 1));
            WmvIpcClient.TerrainTileReport r52 = hp.For(52);
            bool tileStays = WmvRuntimeTerrainTile.Live == live0 + 1 && hp.Session.ShownLoad == 52 && TerrainTallies() == OneShadedTile;
            hp.Answer(hp.Last(WmvTerrainSession.RoleWdt), wdt);
            hp.Answer(hp.Last(WmvTerrainSession.RoleRootAdt), rootNorth);
            hp.Pump(3);
            WmvIpcClient.TerrainTileReport r53 = hp.For(53);
            Check(r50 != null && r50.Status == "superseded" && late && r51 != null && r51.Status == "superseded" && oneAdopted && r52 != null &&
                  r52.Status == "superseded" && tileStays && r53 != null && r53.Status == "built" && hp.Session.ShownLoad == 53 &&
                  WmvRuntimeTerrainTile.Live == live0 + 1 && hp.CountFor(50) == 1 && hp.CountFor(51) == 1 && hp.CountFor(52) == 1 &&
                  hp.CountFor(53) == 1 && supersededNone == "0/0/0" && TerrainTallies() == OneShadedTile,
                  "terrain session: a newer load answers the load in flight \"superseded\" once -- waiting for its WDT (a late answer dropped), " +
                  "with its worker result queued (dropped), adopted but not yet reported (its tile stays until the newer commits); the tiles' " +
                  "tallies " + supersededNone + " after the loads superseded before a build, one tile's while one is shown (" +
                  TerrainTallies() + ")", log);
            hp.Session.LeaveForModelLoad("test model load");
            Check(clean() && !hp.View.ViewOn && hp.View.LastLeaveForModelLoad && hp.Reports.Count == 4,
                  "terrain session: a model load after a build takes the tile away without a report, leaving the view for a model load; " +
                  "the tiles' tallies " + TerrainTallies(), log);
            for (int l = 50; l <= 53; l++) loads.Add(new KeyValuePair<TerrainHarness, int>(hp, l));
        }

        // ---- clear and a model load in flight: "cleared" / "superseded" once, nothing left, late answers dropped
        {
            var hc = new TerrainHarness();
            harnesses.Add(hc);
            hc.Session.Load(TerrainCommand(60, tx, ty));
            hc.Answer(hc.Last(WmvTerrainSession.RoleWdt), wdt);
            hc.Session.Clear("test");
            bool lateRoot = !hc.Answer(hc.Last(WmvTerrainSession.RoleRootAdt), root) && hc.Session.LateAnswersDropped == 1;
            hc.Pump(3);
            WmvIpcClient.TerrainTileReport r60 = hc.For(60);
            hc.Session.Load(TerrainCommand(61, tx, ty));
            hc.Session.LeaveForModelLoad("test model load");
            hc.Pump(3);
            WmvIpcClient.TerrainTileReport r61 = hc.For(61);
            hc.Session.Clear("nothing in flight");
            Check(r60 != null && r60.Status == "cleared" && r60.Reason == "clearTerrain: test" && lateRoot && hc.CountFor(60) == 1 && r61 != null &&
                  r61.Status == "superseded" && r61.Reason == "test model load" && hc.CountFor(61) == 1 && hc.Reports.Count == 2 && clean() &&
                  hc.View.Left == 0,
                  "terrain session: clearTerrain in flight answers \"cleared\" once, a model load \"superseded\" once, a clear with nothing " +
                  "in flight answers nothing; the tiles' tallies " + TerrainTallies(), log);
            loads.Add(new KeyValuePair<TerrainHarness, int>(hc, 60));
            loads.Add(new KeyValuePair<TerrainHarness, int>(hc, 61));
        }

        // ---- after the commit: a failure finishing the adoption still reports "built", with what went wrong
        {
            var ha = new TerrainHarness();
            harnesses.Add(ha);
            ha.View.FailShow = true;
            WmvIpcClient.TerrainTileReport r = TerrainBuild(ha, 70, tx, ty, wdt, root);
            Check(r != null && r.Status == "built" && r.Reason.StartsWith("adopted, but finishing the adoption failed:") &&
                  WmvRuntimeTerrainTile.Live == live0 + 1 && ha.Session.ViewActive,
                  "terrain session: a failure after the commit reports \"built\" with \"adopted, but finishing the adoption failed\" (" +
                  (r != null ? r.Reason : "no report") + ")", log);
            ha.View.FailShow = false;
            // A failure before the commit leaves the tile on screen and the view as they were.
            ha.View.FailBuild = true;
            ha.Session.Load(TerrainCommand(71, tx, ty - 1));
            ha.Answer(ha.Last(WmvTerrainSession.RoleWdt), wdt);
            ha.Answer(ha.Last(WmvTerrainSession.RoleRootAdt), rootNorth);
            ha.Pump(3);
            WmvIpcClient.TerrainTileReport r71 = ha.For(71);
            Check(r71 != null && r71.Status == "failed" && WmvRuntimeTerrainTile.Live == live0 + 1 && ha.Session.ShownLoad == 70 &&
                  ha.View.ViewOn && ha.View.Left == 0 && Meshes() == meshes0 + 256 && Materials() == materials0 + 1 &&
                  r71.Reason == "building the tile's Unity objects failed: InvalidOperationException: test: the build fails after its objects exist",
                  "terrain session: a failure before the commit releases the new tile and leaves the shown one and the view as they were (" +
                  (r71 != null ? r71.Reason : "no report") + ")", log);
            Check(ha.View.TalliesAtBuild == "512/2/514" && TerrainTallies() == OneShadedTile && r71 != null && r71.TerrainGameObjects == 257,
                  "terrain session: the tiles' tallies count a tile built but not committed (" + ha.View.TalliesAtBuild + " with the shown one), " +
                  "drop it with its release (" + TerrainTallies() + "), and the failed report is sampled after it (terrainGameObjects " +
                  (r71 != null ? r71.TerrainGameObjects.ToString() : "none") + ")", log);
            ha.View.FailBuild = false;
            ha.Session.Clear("test");
            loads.Add(new KeyValuePair<TerrainHarness, int>(ha, 70));
            loads.Add(new KeyValuePair<TerrainHarness, int>(ha, 71));
        }

        // ---- the player's own disposal (Dispose: Object.Destroy, deferred to the end of the frame): the tallies drop at once
        {
            var hd = new TerrainHarness();
            harnesses.Add(hd);
            hd.View.Deferred = true;
            WmvIpcClient.TerrainTileReport r = TerrainBuild(hd, 80, tx, ty, wdt, root);
            bool shownTallies = TerrainTallies() == OneShadedTile;
            hd.Session.Clear("test");
            string afterClear = TerrainTallies();
            int meshesPending = Meshes() - meshes0, materialsPending = Materials() - materials0;
            Check(r != null && r.Status == "built" && shownTallies && afterClear == "0/0/0" && WmvRuntimeTerrainTile.Live == live0 &&
                  meshesPending == 256 && materialsPending == 1,
                  "terrain session: the player's own disposal (Object.Destroy, deferred to the end of the frame) takes the tile off the tallies " +
                  "at once -- " + afterClear + " while Unity still counts its " + meshesPending + " Mesh(es) and " + materialsPending +
                  " Material(s) until the frame ends", log);
            hd.View.DestroyPendingNow();
            Check(clean(), "terrain session: ... and what Unity still counted was that tile's: released now, the counts are back (" +
                  (Meshes() - meshes0) + " Mesh(es), " + (Materials() - materials0) + " Material(s) over)", log);
            loads.Add(new KeyValuePair<TerrainHarness, int>(hd, 80));
        }

        // ---- exactly one report per load, never a second; the world model untouched; nothing left
        int wrong = 0, refused = 0;
        foreach (var kv in loads)
            if (kv.Key.CountFor(kv.Value) != 1) wrong++;
        foreach (TerrainHarness x in harnesses)
        {
            refused += x.Session.SecondReportsRefused;
            x.Session.Dispose();
        }
        Check(wrong == 0 && refused == 0,
              "terrain session: every one of " + loads.Count + " loads was reported exactly once (" + wrong + " not; " + refused + " second report(s) refused)", log);
        Check(wmo.Root != null && !wmo.Root.activeSelf && wmo.Meshes.Length == 3 && wmo.Meshes[0] != null && WmvRuntimeMapObject.Live == wmoLive && WmvRuntimeModel.Live == modelLive && clean(),
              "terrain session: the world model beside it is untouched (live world models " + WmvRuntimeMapObject.Live + ", models " +
              WmvRuntimeModel.Live + "), and every tile, Mesh and Material of the session tests is gone", log);
        // Released at once: the counts these tests compare are taken within this frame.
        UnityEngine.Object.DestroyImmediate(wmo.Root);
        foreach (Mesh m in wmo.Meshes) if (m != null) UnityEngine.Object.DestroyImmediate(m);
        foreach (Material m in wmo.Materials) if (m != null) UnityEngine.Object.DestroyImmediate(m);
        if (wmo.FallbackMaterial != null) UnityEngine.Object.DestroyImmediate(wmo.FallbackMaterial);
        foreach (Texture2D t in wmo.Textures) if (t != null) UnityEngine.Object.DestroyImmediate(t);
        wmo.Dispose();
    }

    /// <summary>The camera through the terrain view (5.6): a model framed and zoomed, the view entered (TerrainViewMask), the
    /// tile framed as a world model and the top-down camera applied (2.3b: screen up +Z, right +X, half-size T/2), then left
    /// -- the orbit state, the transform, the projection, the input and the framing record exactly as before,
    /// ModelViewMask back, and the next load owed a framing.</summary>
    static void TerrainCameraTests(Action<string> log)
    {
        GameObject go;
        WmvOrbitCamera orbit = NewCamera(out go);
        try
        {
            Camera cam = go.GetComponent<Camera>();
            cam.enabled = false;
            cam.aspect = 1f;
            cam.cullingMask = WmvTerrainBuilder.ModelViewMask;
            orbit.Frame(new Bounds(new Vector3(1f, 2f, 3f), new Vector3(2f, 3f, 1f)));
            orbit.ZoomByNotches(2f);
            orbit.AdvanceZoom(0.05f);              // part of the way: the live and target distances differ
            orbit.SetView(orbit.yaw, orbit.pitch, 0f);   // the transform placed from that state (the distance kept)
            WmvOrbitCamera.State before = orbit.Snapshot();
            Vector3 position = go.transform.position;
            Quaternion rotation = go.transform.rotation;
            float near = cam.nearClipPlane, far = cam.farClipPlane;
            var framing = new WmvTerrainViewMode.Framing { Box = new Bounds(new Vector3(1f, 2f, 3f), Vector3.one), Aspect = 1.25f, Have = true, Kept = true };
            var mode = new WmvTerrainViewMode(orbit, cam);
            bool entered = mode.Enter(framing) && mode.Active && cam.cullingMask == WmvTerrainBuilder.TerrainViewMask;
            bool once = !mode.Enter(new WmvTerrainViewMode.Framing());
            float t = (float)TerrainCoordinates.TileSize;
            orbit.FrameMapObject(new Bounds(new Vector3(t * 0.5f, 90f, -t * 0.5f), new Vector3(t, 40f, t)));
            bool moved = !orbit.Snapshot().SameAs(before);
            mode.ApplyTopDown(70f, 110f);
            bool topDown = mode.TopDown && cam.orthographic && cam.orthographicSize == t * 0.5f && !orbit.enabled &&
                           Vector3.Distance(go.transform.up, Vector3.forward) < 1e-6f && Vector3.Distance(go.transform.right, Vector3.right) < 1e-6f &&
                           Vector3.Distance(go.transform.forward, Vector3.down) < 1e-6f && go.transform.position.x == t * 0.5f &&
                           go.transform.position.z == -t * 0.5f && go.transform.position.y > 110f && cam.nearClipPlane > 0f &&
                           go.transform.position.y - cam.farClipPlane < 70f;
            WmvTerrainViewMode.Framing back;
            bool owed = mode.Leave(out back);
            Check(entered && once && moved && topDown,
                  "terrain camera: entering takes TerrainViewMask once; the tile is framed; top-down looks straight down with north up, east right, " +
                  "half-size T/2, the orbit's input off, the planes around the heights", log);
            Check(owed && orbit.Snapshot().SameAs(before) && go.transform.position == position && go.transform.rotation == rotation &&
                  cam.nearClipPlane == near && cam.farClipPlane == far && !cam.orthographic && orbit.enabled &&
                  cam.cullingMask == WmvTerrainBuilder.ModelViewMask && back.Box == framing.Box && back.Aspect == framing.Aspect &&
                  back.Have && back.Kept && !mode.Active,
                  "terrain camera: leaving restores the orbit state (" + orbit.Snapshot() + "), the transform, the planes, the projection, the input and " +
                  "the framing record exactly, puts ModelViewMask back, and owes the next load a framing", log);
            WmvTerrainViewMode.Framing none;
            Check(!mode.Leave(out none) && cam.cullingMask == WmvTerrainBuilder.ModelViewMask,
                  "terrain camera: leaving again changes nothing", log);
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(go);
        }
    }

    /// <summary>Protocol 12 as the IPC client reads and writes it: loadTerrainTile (the cache's epoch, no generation),
    /// clearTerrain, getMapAsset, an assetResponse with state, detail, readMs and md5 (binary payload, never cached) and a
    /// refused one, terrainTileLoaded and runtimeState's terrain fields -- and one session load end to end through the
    /// client's own lines.</summary>
    static void TerrainIpcTests(Action<string> log)
    {
        int liveBefore = WmvRuntimeTerrainTile.Live;
        var go = new GameObject("TerrainIpcTest");
        try
        {
            var ipc = go.AddComponent<WmvIpcClient>();
            var sent = new List<string>();
            var answers = new List<WmvIpcClient.AssetResponse>();
            var modelAnswers = new List<WmvIpcClient.AssetResponse>();
            WmvIpcClient.TerrainTileCommand got = null;
            string cleared = null;
            ipc.SentForTest = s => sent.Add(s);
            ipc.OnMapAssetResponse = r => answers.Add(r);
            ipc.OnAssetResponse = r => modelAnswers.Add(r);
            ipc.OnLoadTerrainTile = c => got = c;
            ipc.OnClearTerrain = r => cleared = r;
            Check(WmvIpcClient.ProtocolVersion == 17, "terrain ipc: the player speaks protocol 17 (13 added terrain textures, 14 the Maps viewer, " +
                  "15 its overhead map and free camera, 16 world models in the world, 17 doodads)", log);

            int generation = ipc.Cache.Generation;
            ipc.FeedLineForTest("{\"type\":\"loadTerrainTile\",\"load\":3,\"assetEpoch\":5,\"mapId\":0,\"mapDirectory\":\"Azeroth\",\"product\":\"wow\"," +
                                "\"build\":\"12.1.0.69933\",\"wdtFileDataID\":775971,\"wdtResolvedBy\":\"path\",\"wdtPath\":\"world/maps/azeroth/azeroth.wdt\"," +
                                "\"tileX\":32,\"tileY\":50,\"maxAssetBytes\":4194304}", null);
            Check(got != null && got.Load == 3 && got.AssetEpoch == 5 && got.MapId == 0 && got.MapDirectory == "Azeroth" && got.Product == "wow" &&
                  got.Build == "12.1.0.69933" && got.WdtFileDataID == 775971 && got.WdtResolvedBy == "path" &&
                  got.WdtPath == "world/maps/azeroth/azeroth.wdt" && got.TileX == 32 && got.TileY == 50 && got.MaxAssetBytes == 4194304,
                  "terrain ipc: loadTerrainTile is read whole (" + got + ")", log);
            Check(ipc.Cache.Epoch == 5 && ipc.Cache.Generation == generation,
                  "terrain ipc: loadTerrainTile states the cache's epoch and begins no generation (the hidden model's files stay)", log);
            ipc.FeedLineForTest("{\"type\":\"clearTerrain\",\"reason\":\"client replaced\"}", null);
            Check(cleared == "client replaced", "terrain ipc: clearTerrain is read with its reason", log);

            string id = ipc.RequestMapAsset("wdt", 775971, 3, 5);
            Check(sent.Count == 1 && sent[0] == "{\"type\":\"getMapAsset\",\"requestId\":\"" + id + "\",\"load\":3,\"assetEpoch\":5,\"role\":\"wdt\",\"fileDataID\":775971}",
                  "terrain ipc: getMapAsset names the load, the epoch, the role and the file (" + (sent.Count > 0 ? sent[0] : "nothing sent") + ")", log);
            byte[] payload = { 1, 2, 3, 4, 5 };
            string md5 = WmvTerrainSession.Md5Hex(payload);
            ipc.FeedLineForTest("{\"type\":\"assetResponse\",\"requestId\":\"" + id + "\",\"ok\":true,\"fileDataID\":775971,\"role\":\"wdt\",\"load\":3," +
                                "\"assetEpoch\":5,\"state\":\"available\",\"detail\":\"\",\"readMs\":1.25,\"md5\":\"" + md5 + "\",\"byteLength\":5," +
                                "\"encoding\":\"binary\",\"payloadBytes\":5}", payload);
            WmvIpcClient.AssetResponse a = answers.Count > 0 ? answers[0] : null;
            Check(a != null && a.ok && a.requestId == id && a.data == payload && a.state == "available" && a.readMs == 1.25 && a.md5 == md5 &&
                  a.role == "wdt" && a.load == 3 && a.assetEpoch == 5 && a.fileDataID == 775971 && !a.fromCache && !ipc.Cache.Holds(775971) &&
                  modelAnswers.Count == 0 && id.StartsWith("map", StringComparison.Ordinal),
                  "terrain ipc: the map file's answer (request " + id + ") carries its bytes, state, readMs, md5, role, load and epoch, comes " +
                  "through OnMapAssetResponse only, and is never cached", log);
            ipc.FeedLineForTest("{\"type\":\"assetResponse\",\"requestId\":\"u77\",\"ok\":false,\"fileDataID\":787450,\"role\":\"rootAdt\",\"load\":3," +
                                "\"assetEpoch\":5,\"state\":\"not installed locally\",\"detail\":\"listed, not on this computer\",\"readMs\":0.5," +
                                "\"error\":\"listed, not on this computer\"}", null);
            WmvIpcClient.AssetResponse refused = answers.Count > 1 ? answers[1] : null;
            Check(refused != null && !refused.ok && refused.data == null && refused.state == "not installed locally" &&
                  refused.detail == "listed, not on this computer" && refused.error == "listed, not on this computer" && refused.readMs == 0.5 &&
                  modelAnswers.Count == 0,
                  "terrain ipc: a refused map file's answer carries the state, the detail and error (and, carrying a role, goes to the map " +
                  "handler although its id is not a map id)", log);

            var report = new WmvIpcClient.TerrainTileReport
            {
                Load = 3, Status = "built", MapId = 0, TileX = 32, TileY = 50, WdtFileDataID = 775971, RootFileDataID = 778037, WdtBytes = 294988,
                RootBytes = 329922, WdtMd5 = "aa", RootMd5 = "bb", HasMesh = true, Chunks = 256, ChunksBuilt = 256, Vertices = 37120,
                Triangles = 65536, HasBounds = true, BoundsMin = new Vector3(0f, -1.5f, -533.3333f), BoundsMax = new Vector3(533.3333f, 2f, 0f),
                OriginX = 9600.0, OriginY = 0.0, MeshBytes = 1284096, WdtRoundTripMs = 30.5, TotalMs = 120.25, FirstFrameMs = 33,
                LiveTerrainTiles = 1, MeshObjects = 300, MaterialObjects = 40, TerrainGameObjects = 257,
            };
            string line = WmvIpcClient.TerrainTileLoadedLine(report);
            report.Status = "failed";
            report.Reason = "the host answered \"stale\"";
            report.HostState = "stale";
            report.HostDetail = "request epoch 4, current 5";
            report.HasMesh = false;
            report.WdtRoundTripMs = -1;
            report.FirstFrameMs = -1;
            string failedLine = WmvIpcClient.TerrainTileLoadedLine(report);
            Check(line.StartsWith("{\"type\":\"terrainTileLoaded\",\"load\":3,\"status\":\"built\",\"reason\":\"\"") && line.Contains("\"chunks\":256") &&
                  line.Contains("\"vertices\":37120") && line.Contains("\"boundsMin\":[0,-1.5,-533.3333]") && line.Contains("\"rootOffset\":[0,0,0]") &&
                  line.Contains("\"originWorld\":[9600,0,0]") && line.Contains("\"timings\":{\"wdtRoundTripMs\":30.5,\"firstFrameMs\":33,\"totalMs\":120.25}") &&
                  line.Contains("\"wdtMd5\":\"aa\"") && line.Contains("\"terrainGameObjects\":257") && !line.Contains("hostState") &&
                  failedLine.Contains("\"hostState\":\"stale\",\"hostDetail\":\"request epoch 4, current 5\"") && !failedLine.Contains("\"chunks\"") &&
                  !failedLine.Contains("boundsMin") && failedLine.Contains("\"reason\":\"the host answered \\\"stale\\\"\"") &&
                  failedLine.Contains("\"timings\":{\"totalMs\":120.25}") && JsonUtility.FromJson<TerrainLineProbe>(line).load == 3 &&
                  JsonUtility.FromJson<TerrainLineProbe>(failedLine).status == "failed",
                  "terrain ipc: terrainTileLoaded carries what the load reached and leaves out the rest (" + failedLine + ")", log);

            sent.Clear();
            ipc.ReportRuntimeState(9, new WmvIpcClient.RuntimeReport
            {
                MountKey = "", LiveTerrainTiles = 1, TerrainLoad = 3, TerrainLoading = false, TerrainView = true,
                CameraMask = WmvTerrainBuilder.TerrainViewMask, MeshObjects = 300, MaterialObjects = 40,
                TerrainMeshes = 256, TerrainMaterials = 1, TerrainGameObjects = 257,
                TerrainPendingRequests = 1, TerrainRetiredRequests = 3, MapAnswersDropped = 4, TerrainTimeoutMs = 3000,
                TerrainRetiredTotal = 37, TerrainLastRetiredRequest = "map41", MapAnswersLate = 3, MapAnswersUnowned = 1,
            });
            string stateLine = sent.Count == 1 ? sent[0] : "";
            sent.Clear();
            ipc.ReportRuntimeState(10, new WmvIpcClient.RuntimeReport { MountKey = "" });
            string noneLine = sent.Count == 1 ? sent[0] : "";
            Check(stateLine.Contains(",\"liveTerrainTiles\":1,\"terrainLoad\":3,\"terrainLoading\":false,\"terrainView\":true," +
                                     "\"cameraMask\":1073741824,\"meshObjects\":300,\"materialObjects\":40," +
                                     "\"terrainMeshes\":256,\"terrainMaterials\":1,\"terrainGameObjects\":257," +
                                     "\"terrainPendingRequests\":1,\"terrainRetiredRequests\":3,\"terrainRetiredTotal\":37," +
                                     "\"terrainLastRetiredRequest\":\"map41\",\"mapAnswersRouted\":" + ipc.MapAnswersRouted +
                                     ",\"mapAnswersDropped\":4,\"mapAnswersLate\":3,\"mapAnswersUnowned\":1,\"terrainTimeoutMs\":3000," +
                                     "\"terrainTextures\":0,\"terrainTextureBytes\":0,\"terrainAlphaArrays\":0,") &&
                  ipc.MapAnswersRouted == 2 && noneLine.Contains(",\"terrainLastRetiredRequest\":\"\",") &&
                  JsonUtility.FromJson<RuntimeStateProbe>(stateLine).terrainLastRetiredRequest == "map41" &&
                  JsonUtility.FromJson<RuntimeStateProbe>(stateLine).terrainRetiredTotal == 37,
                  "terrain ipc: runtimeState carries the terrain fields under the host's names (terrainMeshes, terrainMaterials, " +
                  "terrainGameObjects, terrainPendingRequests, terrainRetiredRequests, terrainRetiredTotal, terrainLastRetiredRequest " +
                  "-- a JSON string, \"\" for none --, mapAnswersRouted, mapAnswersDropped, mapAnswersLate, mapAnswersUnowned and " +
                  "terrainTimeoutMs included), the flags as JSON booleans (" +
                  (stateLine.Length > 0 ? stateLine.Substring(Math.Max(0, stateLine.Length - 260)) : "nothing sent") + ")", log);

            // One load end to end through the client: the getMapAsset lines it sends, the answers as host lines, the report it sends.
            sent.Clear();
            var view = new TestTerrainView();
            var session = new WmvTerrainSession(view, ipc.RequestMapAsset, ipc.ReportTerrainTileLoaded, null, x => x());
            ipc.OnMapAssetResponse = r => { if (!session.ClaimMapAssetResponse(r)) answers.Add(r); };
            int tx = TerrainTestTileX, ty = TerrainTestTileY;
            byte[] wdt = TerrainWdt(true, tx, ty, (int)TerrainTestRoot);
            byte[] root = AdtSynthetic.FlatRoot(tx, ty);
            session.Load(TerrainCommand(4, tx, ty));
            string wdtLine = sent.Count > 0 ? sent[sent.Count - 1] : "";
            string wdtId = RequestIdOf(wdtLine);
            ipc.FeedLineForTest(MapAnswerLine(wdtId, TerrainTestWdt, "wdt", 4, wdt), wdt);
            string rootLine = sent.Count > 0 ? sent[sent.Count - 1] : "";
            string rootId = RequestIdOf(rootLine);
            ipc.FeedLineForTest(MapAnswerLine(rootId, (int)TerrainTestRoot, "rootAdt", 4, root), root);
            for (int f = 0; f < 3; f++)
                session.Pump(100.0 + f);
            string reportLine = sent.Count > 0 ? sent[sent.Count - 1] : "";
            Check(wdtLine.Contains("\"role\":\"wdt\",\"fileDataID\":775971") && wdtLine.Contains("\"load\":4,\"assetEpoch\":7") &&
                  rootLine.Contains("\"role\":\"rootAdt\",\"fileDataID\":778037") && reportLine.StartsWith("{\"type\":\"terrainTileLoaded\",\"load\":4,\"status\":\"built\"") &&
                  reportLine.Contains("\"chunks\":256") && reportLine.Contains("\"rootMd5\":\"" + WmvTerrainSession.Md5Hex(root) + "\"") &&
                  answers.Count == 2 && sent.Count == 3 && modelAnswers.Count == 0 && session.PendingRequests == 0 && session.RetiredRequests == 0 &&
                  session.AnswersDropped == 0,
                  "terrain ipc: a load end to end through the client's own lines -- getMapAsset wdt, then rootAdt, then terrainTileLoaded built (" +
                  (reportLine.Length > 120 ? reportLine.Substring(0, 120) + "..." : reportLine) + ")", log);
            session.Clear("test");
            Check(sent.Count == 3 && WmvRuntimeTerrainTile.Live == liveBefore && view.Disposed == 1,
                  "terrain ipc: clearTerrain after the build sends nothing and releases the tile", log);
            session.Dispose();
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(go);
        }
    }

    /// <summary>
    /// The terrain session's request lifetime (stage 03 follow-up). The load in flight waits for ONE request; when the
    /// load ends by any path -- built, failed (the timeout included), superseded, cleared, a model load, shutdown -- that
    /// request goes to the retired ring (RetiredCapacity), and an answer that comes afterwards is dropped as late: never
    /// applied (no further request goes out, nothing is built), never a second report. An answer naming the request
    /// waited for but another load is dropped and the request still waits. Whatever the host does, the bookkeeping stays
    /// bounded and accounted for: through 1200 loads whose answers never come, come refused or come late, every load is
    /// reported exactly once, every load that ends with its request unanswered retires exactly that request (RetiredTotal
    /// one higher, the ring naming that load), every request sent is an answer applied, a retirement or the one pending,
    /// at most 32 are retired, and no collection of the session or of the load in flight grows.
    /// </summary>
    static void TerrainRequestLifetimeTests(Action<string> log)
    {
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        byte[] wdt = TerrainWdt(true, tx, ty, (int)TerrainTestRoot);
        byte[] root = AdtSynthetic.FlatRoot(tx, ty);
        int live0 = WmvRuntimeTerrainTile.Live;
        string rw = WmvTerrainSession.RoleWdt, rr = WmvTerrainSession.RoleRootAdt;
        var harnesses = new List<TerrainHarness>();
        Func<TerrainHarness, string> state = x => string.Format("pending {0}, retired {1} ({2} in all, last \"{3}\"), late {4}, unowned {5}, " +
            "reports {6}, requests sent {7}", x.Session.PendingRequests, x.Session.RetiredRequests, x.Session.RetiredTotal,
            x.Session.LastRetiredRequest, x.Session.LateAnswersDropped, x.Session.UnownedAnswersDropped, x.Reports.Count, x.Sent.Count);

        // The collection count SessionCollections relies on: every kind of collection, a string none.
        int nHash, nReadOnly, nEnumerable, nQueue, nString;
        bool countedHash = TryCountCollection(new HashSet<string> { "a", "b", "c" }, out nHash);
        bool countedReadOnly = TryCountCollection(new CountOnlyCollection(), out nReadOnly);
        bool countedEnumerable = TryCountCollection(Yield(4), out nEnumerable);
        bool countedQueue = TryCountCollection(new Queue<int>(new[] { 1 }), out nQueue);
        bool counted = countedHash && countedReadOnly && countedEnumerable && countedQueue && !TryCountCollection("text", out nString);
        Check(counted && nHash == 3 && nReadOnly == 2 && nEnumerable == 4 && nQueue == 1,
              "terrain requests: a collection is counted whatever it implements -- a HashSet<string> (generic ICollection<T> only) " + nHash +
              ", an IReadOnlyCollection<T> alone by its Count " + nReadOnly + ", a bare IEnumerable by enumerating it " + nEnumerable +
              ", a Queue " + nQueue + "; a string is not one", log);
        int largestStart;
        string collections = SessionCollections(new TerrainHarness().Session, out largestStart);
        Check(typeof(WmvTerrainSession).GetField("requests", BindingFlags.Instance | BindingFlags.NonPublic) == null &&
              WmvTerrainSession.RetiredCapacity == 128 && WmvTerrainSession.MaxOutstanding == 4 && largestStart == 0,
              "terrain requests: the session keeps no table of its requests -- its collections are " + collections, log);
        var clampLow = new TerrainHarness(500.0);
        var clampHigh = new TerrainHarness(500000.0);
        var clampNaN = new TerrainHarness(double.NaN);
        Check(clampLow.Session.TimeoutMs == 1000.0 && clampHigh.Session.TimeoutMs == 120000.0 && clampNaN.Session.TimeoutMs == 30000.0 &&
              new TerrainHarness().Session.TimeoutMs == 30000.0,
              "terrain requests: the timeout defaults to 30000 ms and is kept within 1000..120000 ms (" + clampLow.Session.TimeoutMs + ", " +
              clampHigh.Session.TimeoutMs + ", " + clampNaN.Session.TimeoutMs + ")", log);

        // ---- successful WDT and root answers: one pending at a time, built once, nothing retired or dropped
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            h.Session.Load(TerrainCommand(201, tx, ty));
            int p1 = h.Session.PendingRequests;
            bool wdtApplied = h.Answer(h.Last(rw), wdt);
            int p2 = h.Session.PendingRequests;
            bool rootApplied = h.Answer(h.Last(rr), root);
            int p3 = h.Session.PendingRequests;
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r = h.For(201);
            Check(p1 == 1 && wdtApplied && p2 == 1 && rootApplied && p3 == 0 && r != null && r.Status == "built" && h.CountFor(201) == 1 &&
                  h.Session.RetiredRequests == 0 && h.Session.AnswersDropped == 0 && h.Session.ShownLoad == 201,
                  "terrain requests: the WDT and root answers are applied (pending " + p1 + " -> " + p2 + " -> " + p3 + "), the tile built and " +
                  "reported once -- " + state(h), log);
            h.Session.Clear("test");
        }

        // ---- an answer that never arrives: pending while in flight; the injected 2 s timeout fails the load once and
        // retires its request; the answer that comes after it is dropped as late
        {
            var h = new TerrainHarness(2000.0);
            harnesses.Add(h);
            h.Session.Load(TerrainCommand(202, tx, ty));
            string id = h.Last(rw);
            h.Session.Pump(100.0);
            h.Session.Pump(2100.0);
            bool waiting = h.Reports.Count == 0 && h.Session.PendingRequests == 1 && h.Session.RetiredRequests == 0 && h.Session.Loading &&
                           h.Session.RetiredTotal == 0 && h.Session.LastRetiredRequest == "";
            h.Session.Pump(2100.5);
            WmvIpcClient.TerrainTileReport r = h.For(202);
            string want = "timed out after 2 s waiting for the WDT answer (request " + id + ")";
            WmvTerrainSession.RetiredRequest ended;
            bool inRing = h.Session.TryGetRetired(id, out ended) && ended.Load == 202 && ended.Role == rw && ended.Ended == "failed";
            Check(h.Session.TimeoutMs == 2000.0 && waiting && r != null && r.Status == "failed" && r.Reason == want && h.CountFor(202) == 1 &&
                  h.Session.PendingRequests == 0 && h.Session.RetiredRequests == 1 && inRing && !h.Session.Loading &&
                  h.Session.RetiredTotal == 1 && h.Session.LastRetiredRequest == id,
                  "terrain requests: an answer that never arrives keeps one request pending until the 2 s timeout, which reports \"" +
                  (r != null ? r.Reason : "nothing") + "\" once and retires the request (load " + ended.Load + ", " + ended.Role + ", ended \"" +
                  ended.Ended + "\") -- " + state(h), log);
            bool applied = h.Answer(id, wdt);
            h.Pump(3);
            Check(!applied && h.Session.LateAnswersDropped == 1 && h.Session.UnownedAnswersDropped == 0 && h.CountFor(202) == 1 &&
                  h.Session.SecondReportsRefused == 0 && h.Sent.Count == 1 && h.Session.ShownLoad == 0 && WmvRuntimeTerrainTile.Live == live0,
                  "terrain requests: the answer after the timeout is dropped as late -- not applied (no root asked for, no tile), no second " +
                  "report -- " + state(h), log);
        }

        // ---- the load ends before its answer by clearTerrain, a model load, or the host's clearTerrain for a new client: one
        // report, nothing pending, the request retired as ended; its late answer (the host answers it "available" or, for the
        // replaced client, "stale") dropped as late, no second report, nothing asked for
        Action<string, int, Action<TerrainHarness>, string, string, string> endsFirst = (what, load, end, status, reason, lateState) =>
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            h.Session.Load(TerrainCommand(load, tx, ty));
            string id = h.Last(rw);
            end(h);
            WmvIpcClient.TerrainTileReport r = h.For(load);
            WmvTerrainSession.RetiredRequest ended;
            bool retiredOk = h.Session.TryGetRetired(id, out ended) && ended.Load == load && ended.Ended == status;
            bool once = r != null && r.Status == status && r.Reason == reason && h.CountFor(load) == 1 && h.Session.PendingRequests == 0 &&
                        h.Session.RetiredRequests == 1 && retiredOk && !h.Session.Loading && h.Session.RetiredTotal == 1 &&
                        h.Session.LastRetiredRequest == id;
            bool applied = lateState == "available" ? h.Answer(id, wdt) : h.Answer(id, null, lateState, "request epoch 7, current 8");
            h.Pump(3);
            Check(once && !applied && h.Session.LateAnswersDropped == 1 && h.CountFor(load) == 1 && h.Session.SecondReportsRefused == 0 &&
                  h.Sent.Count == 1,
                  "terrain requests: " + what + " before the answer -> one \"" + status + "\" (" + (r != null ? r.Reason : "no report") +
                  "), the request retired; its late \"" + lateState + "\" answer dropped as late -- " + state(h), log);
        };
        endsFirst("clearTerrain", 203, x => x.Session.Clear("test"), "cleared", "clearTerrain: test", "available");
        endsFirst("a model load", 204, x => x.Session.LeaveForModelLoad("test model load"), "superseded", "test model load", "available");
        endsFirst("the host's clearTerrain for a replaced client", 205, x => x.Session.Clear("the client was replaced"), "cleared",
                  "clearTerrain: the client was replaced", "stale");

        // ---- a new client epoch before the answer: the old load cleared, the new epoch's load built; the old load's "stale"
        // answer arriving in the middle of the new load is dropped as late and leaves the new load's request waiting
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            h.Session.Load(TerrainCommand(206, tx, ty));
            string oldId = h.Last(rw);
            h.Session.Clear("the client was replaced");
            WmvIpcClient.TerrainTileCommand next = TerrainCommand(207, tx, ty);
            next.AssetEpoch = 8;
            h.Session.Load(next);
            string newId = h.Last(rw);
            bool lateApplied = h.Answer(oldId, null, "stale", "request epoch 7, current 8");
            bool newWaits = h.Session.PendingRequests == 1 && h.Session.LoadInFlight == 207 && h.Sent.Count == 2;
            h.Answer(newId, wdt);
            h.Answer(h.Last(rr), root);
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r206 = h.For(206), r207 = h.For(207);
            Check(r206 != null && r206.Status == "cleared" && !lateApplied && newWaits && r207 != null && r207.Status == "built" &&
                  h.CountFor(206) == 1 && h.CountFor(207) == 1 && h.Session.LateAnswersDropped == 1 && h.Session.PendingRequests == 0 &&
                  h.Session.RetiredRequests == 1 && h.LastSent(rw)[4] == "8",
                  "terrain requests: a client switch before the answer -- the old load \"cleared\" once, its \"stale\" answer dropped as late " +
                  "while the new epoch's load waits for its own, which builds -- " + state(h), log);
            h.Session.Clear("test");
        }

        // ---- supersede before the answer: the superseded load's late answer (even "available") is dropped, the newer load
        // still waits for its own request and builds
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            h.Session.Load(TerrainCommand(208, tx, ty));
            string first = h.Last(rw);
            h.Session.Load(TerrainCommand(209, tx, ty));
            string second = h.Last(rw);
            WmvIpcClient.TerrainTileReport r208 = h.For(208);
            WmvTerrainSession.RetiredRequest ended, none;
            bool afterSupersede = r208 != null && r208.Status == "superseded" && h.Session.PendingRequests == 1 && h.Session.RetiredRequests == 1 &&
                                  h.Session.TryGetRetired(first, out ended) && ended.Ended == "superseded" && !h.Session.TryGetRetired(second, out none);
            bool lateApplied = h.Answer(first, wdt);
            bool stillWaits = h.Session.PendingRequests == 1 && h.Session.LoadInFlight == 209 && h.Sent.Count == 2;
            h.Answer(second, wdt);
            h.Answer(h.Last(rr), root);
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r209 = h.For(209);
            Check(afterSupersede && !lateApplied && h.Session.LateAnswersDropped == 1 && stillWaits && r209 != null && r209.Status == "built" &&
                  h.CountFor(208) == 1 && h.CountFor(209) == 1 && h.Session.PendingRequests == 0,
                  "terrain requests: a load superseded before its answer is reported once, its request retired; the late answer is dropped " +
                  "and the newer load, still waiting for its own, builds -- " + state(h), log);
            h.Session.Clear("test");
        }

        // ---- the right request id under another load: dropped (unowned), the request still waits; the real answer applies
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            h.Session.Load(TerrainCommand(210, tx, ty));
            string id = h.Last(rw);
            bool wrongApplied = h.Answer(id, wdt, "available", "", null, 999);
            bool waits = h.Session.PendingRequests == 1 && h.Reports.Count == 0 && h.Sent.Count == 1 && h.Session.UnownedAnswersDropped == 1;
            bool rightApplied = h.Answer(id, wdt);
            h.Answer(h.Last(rr), root);
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r = h.For(210);
            Check(!wrongApplied && waits && rightApplied && r != null && r.Status == "built" && h.CountFor(210) == 1 &&
                  h.Session.LateAnswersDropped == 0,
                  "terrain requests: an answer naming the waited-for request but load 999 is dropped (unowned) and the request keeps waiting; " +
                  "its real answer is applied and the tile built -- " + state(h), log);
            h.Session.Clear("test");
        }

        // ---- shutdown with a request out (the player going away, as on a reconnect): no report, the request retired as
        // "dropped at shutdown", its answer dropped as late; a new session (the next player) starts with nothing and builds
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            h.Session.Load(TerrainCommand(211, tx, ty));
            string id = h.Last(rw);
            h.Session.Dispose();
            WmvTerrainSession.RetiredRequest ended;
            bool retiredOk = h.Session.TryGetRetired(id, out ended) && ended.Ended == "dropped at shutdown" && ended.Load == 211;
            bool applied = h.Answer(id, wdt);
            var fresh = new TerrainHarness();
            harnesses.Add(fresh);
            bool freshEmpty = fresh.Session.PendingRequests == 0 && fresh.Session.RetiredRequests == 0;
            WmvIpcClient.TerrainTileReport built = TerrainBuild(fresh, 212, tx, ty, wdt, root);
            Check(h.Reports.Count == 0 && h.Session.PendingRequests == 0 && h.Session.RetiredRequests == 1 && retiredOk && !applied &&
                  h.Session.LateAnswersDropped == 1 && h.Sent.Count == 1 && freshEmpty && built != null && built.Status == "built",
                  "terrain requests: shutdown with a request out sends no report and retires it (\"" + ended.Ended + "\"); its answer is dropped " +
                  "as late; the next session starts empty and builds -- " + state(h), log);
            fresh.Session.Clear("test");
        }

        // ---- the ring is bounded: 140 loads superseded unanswered keep the newest 128 (while RetiredTotal counts all 140);
        // the answer of one that fell out is still dropped (as unowned now), never applied
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            var ids = new List<string>();
            for (int i = 0; i < 140; i++)
            {
                h.Session.Load(TerrainCommand(300 + i, tx, ty));
                ids.Add(h.Last(rw));
            }
            h.Session.Clear("test");
            WmvTerrainSession.RetiredRequest e;
            bool oldestOut = !h.Session.TryGetRetired(ids[0], out e) && !h.Session.TryGetRetired(ids[11], out e);
            bool newestIn = h.Session.TryGetRetired(ids[12], out e) && e.Load == 312 && h.Session.TryGetRetired(ids[139], out e) && e.Load == 439 &&
                            e.Ended == "cleared";
            bool appliedOld = h.Answer(ids[0], wdt);
            bool appliedNew = h.Answer(ids[139], wdt);
            Check(h.Session.RetiredRequests == 128 && oldestOut && newestIn && !appliedOld && !appliedNew && h.Session.UnownedAnswersDropped == 1 &&
                  h.Session.LateAnswersDropped == 1 && h.Reports.Count == 140 && h.Sent.Count == 140 && h.Session.RetiredTotal == 140 &&
                  h.Session.LastRetiredRequest == ids[139],
                  "terrain requests: the retired ring keeps the newest 128 of 140 ended requests, RetiredTotal all 140; an answer to one that " +
                  "fell out is dropped as unowned, one still held as late -- neither applied -- " + state(h), log);
        }

        // ---- 1200 loads whose answers never come or fail -- cleared, timed out, taken by a model load, refused by the host,
        // the root never answered, superseded -- with late and unowned answers in between: every load reported exactly once;
        // every load that ends with its request unanswered retires exactly that request -- the ring names it with its load,
        // role and ending, RetiredTotal rises by exactly one, LastRetiredRequest is its id -- and a load that ends on its own
        // answer, or starts with no load in flight, retires nothing; after every load each request sent is accounted for
        // (answers applied + RetiredTotal + PendingRequests); no answer applied that was not waited for; at most 32 retired;
        // and no collection of the session or of the load in flight larger after 1200 loads than after 102 (both samples
        // taken with a load in flight, so its fields are walked too)
        {
            const int n = 1200;
            var h = new TerrainHarness(1000.0);
            harnesses.Add(h);
            double now = 50000.0;
            int maxRetired = 0, lateFed = 0, foreignFed = 0, lateApplied = 0, rootAnswers = 0, applied = 0, retiredExpected = 0;
            int retireChecks = 0, retireWrong = 0, unbalanced = 0, largest = 0;
            string sizesAt102 = null, sizesAtEnd = null, prevId = null, waiting = null, firstRetireWrong = null, firstUnbalanced = null;
            Action<bool, string> retireCheck = (ok, what) =>
            {
                retireChecks++;
                if (ok) return;
                retireWrong++;
                if (firstRetireWrong == null) firstRetireWrong = what;
            };
            // Request rid of load `load` was retired just now: the ring holds it under that load, role and ending, the total is
            // one higher than `before`, and it is the last one retired.
            Func<string, int, string, string, int, bool> retiredNow = (rid, load, role, how, before) =>
            {
                WmvTerrainSession.RetiredRequest e;
                return h.Session.TryGetRetired(rid, out e) && e.Load == load && e.Role == role && e.Ended == how &&
                       h.Session.RetiredTotal == before + 1 && h.Session.LastRetiredRequest == rid;
            };
            Func<string, int, string, string, int, string> retiredSays = (rid, load, role, how, before) =>
            {
                WmvTerrainSession.RetiredRequest e;
                return string.Format("load {0}: request {1} ({2}) should be retired as \"{3}\" -- the ring {4}; total {5} -> {6}, last \"{7}\"",
                                     load, rid, role, how,
                                     h.Session.TryGetRetired(rid, out e) ? "has it under load " + e.Load + ", " + e.Role + ", \"" + e.Ended + "\""
                                                                         : "does not hold it",
                                     before, h.Session.RetiredTotal, h.Session.LastRetiredRequest);
            };
            for (int i = 0; i < n; i++)
            {
                int load = 1000 + i;
                int before = h.Session.RetiredTotal;
                h.Session.Load(TerrainCommand(load, tx, ty));
                if (waiting != null)                                         // the load before was left waiting: superseded now
                {
                    retiredExpected++;
                    retireCheck(retiredNow(waiting, load - 1, rw, "superseded", before), retiredSays(waiting, load - 1, rw, "superseded", before));
                }
                else
                    retireCheck(h.Session.RetiredTotal == before, "load " + load + ": starting it with no load in flight retired a request (total " +
                                before + " -> " + h.Session.RetiredTotal + ")");
                waiting = null;
                string id = h.Last(rw), endedId = null, endedRole = rw, endedHow = null;
                before = h.Session.RetiredTotal;
                switch (i % 6)
                {
                    case 0: h.Session.Clear("repeat"); endedId = id; endedHow = "cleared"; break;
                    case 1: h.Session.Pump(now += 16.0); h.Session.Pump(now += 1000.5); endedId = id; endedHow = "failed"; break;
                    case 2: h.Session.LeaveForModelLoad("repeat model load"); endedId = id; endedHow = "superseded"; break;
                    case 3:                                                  // refused: the load ends on its own answer
                        if (h.Answer(id, null, "stale", "request epoch 6, current 7")) applied++;
                        break;
                    case 4:                                                  // the WDT applied, the root never answered
                        if (h.Answer(id, wdt)) applied++;
                        rootAnswers++;
                        endedId = h.Last(rr);
                        endedRole = rr;
                        endedHow = "cleared";
                        h.Session.Clear("repeat: the root never answered");
                        break;
                    default: waiting = id; break;                            // superseded by the next load
                }
                if (endedId != null)
                {
                    retiredExpected++;
                    retireCheck(retiredNow(endedId, load, endedRole, endedHow, before), retiredSays(endedId, load, endedRole, endedHow, before));
                }
                else
                {
                    WmvTerrainSession.RetiredRequest e;
                    retireCheck(h.Session.RetiredTotal == before && !h.Session.TryGetRetired(id, out e),
                                "load " + load + ": " + (waiting != null ? "still waiting" : "ended on its own answer") + ", yet a request was retired " +
                                "(total " + before + " -> " + h.Session.RetiredTotal + ", last \"" + h.Session.LastRetiredRequest + "\")");
                }
                if (i % 5 == 1 && prevId != null)
                {
                    lateFed++;
                    if (h.Answer(prevId, wdt))
                    {
                        lateApplied++;
                        applied++;
                    }
                }
                if (i % 11 == 7)
                {
                    foreignFed++;
                    if (h.Session.ClaimMapAssetResponse(new WmvIpcClient.AssetResponse { requestId = "zz" + i, ok = false, state = "refused" }))
                    {
                        lateApplied++;
                        applied++;
                    }
                }
                if (h.Sent.Count != applied + h.Session.RetiredTotal + h.Session.PendingRequests)
                {
                    unbalanced++;
                    if (firstUnbalanced == null)
                        firstUnbalanced = string.Format("after load {0}: {1} sent, {2} applied + {3} retired + {4} pending", load, h.Sent.Count, applied,
                                                        h.Session.RetiredTotal, h.Session.PendingRequests);
                }
                maxRetired = Math.Max(maxRetired, h.Session.RetiredRequests);
                if (i == 101)
                {
                    int l;
                    sizesAt102 = SessionCollections(h.Session, out l);
                }
                if (i == n - 1)
                    sizesAtEnd = SessionCollections(h.Session, out largest);
                prevId = id;
            }
            bool waitingAtEnd = waiting != null;
            int beforeEnd = h.Session.RetiredTotal;
            h.Session.Clear("repeat: end");
            if (waitingAtEnd)
            {
                retiredExpected++;
                retireCheck(retiredNow(waiting, 1000 + n - 1, rw, "cleared", beforeEnd), retiredSays(waiting, 1000 + n - 1, rw, "cleared", beforeEnd));
            }
            int wrong = 0;
            var perLoad = new Dictionary<int, int>();
            foreach (WmvIpcClient.TerrainTileReport r in h.Reports)
            {
                int c;
                perLoad.TryGetValue(r.Load, out c);
                perLoad[r.Load] = c + 1;
            }
            for (int i = 0; i < n; i++)
            {
                int c;
                if (!perLoad.TryGetValue(1000 + i, out c) || c != 1) wrong++;
            }
            Check(wrong == 0 && h.Reports.Count == n && h.Session.ReportsSent == n && h.Session.SecondReportsRefused == 0,
                  "terrain requests: " + n + " failing loads were reported exactly once each (" + wrong + " not; " + h.Session.ReportsSent +
                  " reports sent, " + h.Session.SecondReportsRefused + " second report(s) refused)", log);
            Check(retireWrong == 0 && retireChecks == 2 * n + (waitingAtEnd ? 1 : 0) && h.Session.RetiredTotal == retiredExpected &&
                  retiredExpected > WmvTerrainSession.RetiredCapacity && h.Session.RetiredRequests == WmvTerrainSession.RetiredCapacity &&
                  maxRetired <= WmvTerrainSession.RetiredCapacity,
                  "terrain requests: through " + n + " failing loads each of the " + retiredExpected + " that ended with a request unanswered retired " +
                  "exactly that request (the ring naming its load, role and ending, RetiredTotal one higher, LastRetiredRequest its id) and no " +
                  "other load retired anything (" + retireChecks + " checks, " + retireWrong + " wrong" +
                  (firstRetireWrong != null ? "; first: " + firstRetireWrong : "") + "); RetiredTotal " + h.Session.RetiredTotal + ", the ring " +
                  "at most " + maxRetired + ", " + h.Session.RetiredRequests + " at the end", log);
            Check(unbalanced == 0 && h.Sent.Count == applied + h.Session.RetiredTotal + h.Session.PendingRequests && h.Session.PendingRequests == 0 &&
                  !h.Session.Loading && h.Sent.Count == n + rootAnswers,
                  "terrain requests: after each of the " + n + " loads every request sent was an answer applied, a retirement or the one pending (" +
                  unbalanced + " load(s) not" + (firstUnbalanced != null ? "; first: " + firstUnbalanced : "") + "); in all " + h.Sent.Count +
                  " sent (one per load plus " + rootAnswers + " root requests) = " + applied + " applied + " + h.Session.RetiredTotal + " retired + " +
                  h.Session.PendingRequests + " pending", log);
            Check(lateFed > 0 && lateApplied == 0 && h.Session.LateAnswersDropped > 0 && h.Session.UnownedAnswersDropped > 0 &&
                  WmvRuntimeTerrainTile.Live == live0,
                  "terrain requests: none of the " + lateFed + " late and " + foreignFed + " foreign answers was applied (" + h.Session.LateAnswersDropped +
                  " dropped as late, " + h.Session.UnownedAnswersDropped + " as unowned)", log);
            Check(sizesAt102 != null && sizesAtEnd == sizesAt102 && sizesAt102.Contains("current{") && largest <= WmvTerrainSession.RetiredCapacity,
                  "terrain requests: no collection of the session or of the load in flight grew -- after 102 loads " + sizesAt102 + ", after " + n +
                  " " + sizesAtEnd, log);
        }

        int refused = 0;
        foreach (TerrainHarness x in harnesses)
        {
            refused += x.Session.SecondReportsRefused;
            x.Session.Dispose();
        }
        Check(refused == 0 && WmvRuntimeTerrainTile.Live == live0 && TerrainTallies() == "0/0/0",
              "terrain requests: no second report was ever refused, and every tile these tests built is released (live " +
              WmvRuntimeTerrainTile.Live + ", tallies " + TerrainTallies() + ")", log);
    }

    /// <summary>Every collection the session holds, found by reflection over its fields (so one added later is checked
    /// too), and every collection of the load in flight: a field holding one of the session's own nested classes (the Job
    /// in "current") is walked one level, as "name{...}". An array counts by its length ("name=n[]"), any other collection
    /// by its count ("name=n", TryCountCollection: a HashSet&lt;T&gt; included); a string is not a collection here, and a
    /// collection or load field that is null shows as "name=null". largest is the largest count of a non-array.</summary>
    static string SessionCollections(WmvTerrainSession s, out int largest)
    {
        largest = 0;
        var parts = new List<string>();
        CollectionsOf(s, true, parts, ref largest);
        return string.Join(" ", parts.ToArray());
    }

    static void CollectionsOf(object o, bool walkLoads, List<string> parts, ref int largest)
    {
        foreach (FieldInfo f in o.GetType().GetFields(BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public))
        {
            bool sessionClass = f.FieldType.DeclaringType == typeof(WmvTerrainSession) && f.FieldType.IsClass;
            bool collectionType = typeof(System.Collections.IEnumerable).IsAssignableFrom(f.FieldType) && f.FieldType != typeof(string);
            object v = f.GetValue(o);
            if (v == null)
            {
                if (collectionType || (walkLoads && sessionClass))
                    parts.Add(f.Name + "=null");
                continue;
            }
            if (v is string)
                continue;
            var a = v as Array;
            if (a != null)
            {
                parts.Add(f.Name + "=" + a.Length + "[]");
                continue;
            }
            int count;
            if (TryCountCollection(v, out count))
            {
                parts.Add(f.Name + "=" + count);
                largest = Math.Max(largest, count);
                continue;
            }
            if (walkLoads && sessionClass)
            {
                var inner = new List<string>();
                CollectionsOf(v, false, inner, ref largest);
                parts.Add(f.Name + "{" + string.Join(" ", inner.ToArray()) + "}");
            }
        }
    }

    /// <summary>How many items a collection holds, whatever it implements: a non-generic ICollection by its Count, a generic
    /// ICollection&lt;T&gt; or IReadOnlyCollection&lt;T&gt; (a HashSet&lt;T&gt; is only these) by its Count, read by reflection,
    /// and any other IEnumerable by enumerating it. False for a string and for what is not a collection.</summary>
    static bool TryCountCollection(object v, out int count)
    {
        count = 0;
        if (v == null || v is string)
            return false;
        var plain = v as System.Collections.ICollection;
        if (plain != null)
        {
            count = plain.Count;
            return true;
        }
        foreach (Type i in v.GetType().GetInterfaces())
        {
            if (!i.IsGenericType)
                continue;
            Type d = i.GetGenericTypeDefinition();
            if (d == typeof(ICollection<>) || d == typeof(IReadOnlyCollection<>))
            {
                count = (int)i.GetProperty("Count").GetValue(v, null);
                return true;
            }
        }
        var items = v as System.Collections.IEnumerable;
        if (items == null)
            return false;
        foreach (object item in items)
            count++;
        return true;
    }

    /// <summary>A collection that is an IReadOnlyCollection&lt;T&gt; and nothing more: its Count (2) is not what enumerating it
    /// gives (nothing), so a count taken from Count shows.</summary>
    sealed class CountOnlyCollection : IReadOnlyCollection<int>
    {
        public int Count { get { return 2; } }
        public IEnumerator<int> GetEnumerator() { yield break; }
        System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator() { return GetEnumerator(); }
    }

    /// <summary>A bare IEnumerable (an iterator: no collection interface) of n items.</summary>
    static IEnumerable<int> Yield(int n)
    {
        for (int i = 0; i < n; i++)
            yield return i;
    }

    /// <summary>
    /// The map answers through WmvIpcClient's own dispatch (host lines fed as the reader thread hands them over): a map
    /// answer -- an id in the "map" namespace, or any answer carrying a role, each half shown on its own (a role under an
    /// ordinary id, a map id with no role) -- goes to OnMapAssetResponse only, never to OnAssetResponse or the asset
    /// cache; an ordinary model answer still goes to OnAssetResponse and the cache as before,
    /// even after a map answer named its request id; a map answer with no handler wired is dropped and counted. Late
    /// answers after supersession and after the host's clearTerrain for a replaced client reach the session and are
    /// dropped there; every load has exactly one terrainTileLoaded line.
    /// </summary>
    static void TerrainMapRoutingTests(Action<string> log)
    {
        int liveBefore = WmvRuntimeTerrainTile.Live;
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        byte[] wdt = TerrainWdt(true, tx, ty, (int)TerrainTestRoot);
        byte[] root = AdtSynthetic.FlatRoot(tx, ty);
        var go = new GameObject("TerrainMapRoutingTest");
        WmvTerrainSession session = null;
        try
        {
            var ipc = go.AddComponent<WmvIpcClient>();
            var sent = new List<string>();
            var modelAnswers = new List<WmvIpcClient.AssetResponse>();
            var mapAnswers = new List<WmvIpcClient.AssetResponse>();
            ipc.SentForTest = s => sent.Add(s);
            ipc.OnAssetResponse = r => modelAnswers.Add(r);
            var view = new TestTerrainView();
            session = new WmvTerrainSession(view, ipc.RequestMapAsset, ipc.ReportTerrainTileLoaded, null, x => x());
            WmvTerrainSession s = session;
            ipc.OnMapAssetResponse = r => { mapAnswers.Add(r); s.ClaimMapAssetResponse(r); };
            ipc.OnLoadTerrainTile = c => s.Load(c);
            ipc.OnClearTerrain = reason => s.Clear(reason);
            Func<int, int> reportsFor = load => CountSent(sent, "{\"type\":\"terrainTileLoaded\",\"load\":" + load + ",");

            Check(WmvIpcClient.IsMapRequestId("map1") && !WmvIpcClient.IsMapRequestId("u1") && !WmvIpcClient.IsMapRequestId(null) &&
                  WmvIpcClient.IsMapAnswer("u5", "wdt") && WmvIpcClient.IsMapAnswer("map5", "") && !WmvIpcClient.IsMapAnswer("u5", "") &&
                  !WmvIpcClient.IsMapAnswer("u5", null),
                  "terrain routing: a map answer is one whose id is in the map namespace or that carries a role, nothing else", log);

            // ---- a load through the client's lines: both answers to the map handler only; the asset cache never sees them
            int cacheCount = ipc.Cache.Count;
            long cacheBytes = ipc.Cache.Bytes;
            ipc.FeedLineForTest(TerrainLoadLine(501, 7), null);
            string wdtId = RequestIdOf(LastSent(sent, "getMapAsset"));
            ipc.FeedLineForTest(MapAnswerLine(wdtId, TerrainTestWdt, "wdt", 501, wdt), wdt);
            string rootId = RequestIdOf(LastSent(sent, "getMapAsset"));
            ipc.FeedLineForTest(MapAnswerLine(rootId, (int)TerrainTestRoot, "rootAdt", 501, root), root);
            for (int f = 0; f < 3; f++)
                session.Pump(100.0 + f);
            Check(wdtId.StartsWith("map", StringComparison.Ordinal) && rootId.StartsWith("map", StringComparison.Ordinal) && wdtId != rootId &&
                  reportsFor(501) == 1 && SentContains(sent, "{\"type\":\"terrainTileLoaded\",\"load\":501,\"status\":\"built\"") &&
                  mapAnswers.Count == 2 && modelAnswers.Count == 0 && ipc.MapAnswersRouted == 2 && ipc.Cache.Count == cacheCount &&
                  ipc.Cache.Bytes == cacheBytes && !ipc.Cache.Holds(TerrainTestWdt) && !ipc.Cache.Holds((int)TerrainTestRoot) &&
                  session.PendingRequests == 0,
                  "terrain routing: a load's WDT and root answers (" + wdtId + ", " + rootId + ") go to OnMapAssetResponse only (" +
                  mapAnswers.Count + " there, " + modelAnswers.Count + " to OnAssetResponse), the asset cache untouched, built once", log);

            // ---- a late answer after supersession, then the host's clearTerrain for a replaced client before the answer
            ipc.FeedLineForTest(TerrainLoadLine(502, 7), null);
            string id502 = RequestIdOf(LastSent(sent, "getMapAsset"));
            ipc.FeedLineForTest(TerrainLoadLine(503, 7), null);
            string id503 = RequestIdOf(LastSent(sent, "getMapAsset"));
            int sentBefore = sent.Count;
            ipc.FeedLineForTest(MapAnswerLine(id502, TerrainTestWdt, "wdt", 502, wdt), wdt);
            bool lateDropped = session.LateAnswersDropped == 1 && sent.Count == sentBefore && session.LoadInFlight == 503 &&
                               session.PendingRequests == 1 && modelAnswers.Count == 0;
            ipc.FeedLineForTest("{\"type\":\"clearTerrain\",\"reason\":\"the client was replaced\"}", null);
            ipc.FeedLineForTest(RefusedMapAnswerLine(id503, TerrainTestWdt, "wdt", 503, "stale", "request epoch 7, current 8"), null);
            Check(lateDropped && session.LateAnswersDropped == 2 && reportsFor(502) == 1 && reportsFor(503) == 1 &&
                  SentContains(sent, "{\"type\":\"terrainTileLoaded\",\"load\":502,\"status\":\"superseded\"") &&
                  SentContains(sent, "{\"type\":\"terrainTileLoaded\",\"load\":503,\"status\":\"cleared\"") && modelAnswers.Count == 0 &&
                  session.PendingRequests == 0 && session.RetiredRequests == 2 && WmvRuntimeTerrainTile.Live == liveBefore,
                  "terrain routing: the superseded load's late answer and the cleared load's \"stale\" answer (after the host's clearTerrain " +
                  "for a replaced client) reach the session and are dropped as late; one terrainTileLoaded each, nothing to OnAssetResponse", log);

            // ---- a role-carrying answer under an ordinary request's id: the map handler's (unowned), and the model request it
            // names is untouched -- its cache flight still out; the request's own answer then goes to OnAssetResponse and the cache
            const int modelFile = 990302;
            string modelId = ipc.RequestAssetByFileDataID(modelFile, true);
            bool flying = ipc.Cache.InFlight(modelFile);
            int unowned = session.UnownedAnswersDropped;
            ipc.FeedLineForTest(RefusedMapAnswerLine(modelId, TerrainTestWdt, "wdt", 503, "stale", "a map answer under a model id"), null);
            bool stillFlying = ipc.Cache.InFlight(modelFile) && modelAnswers.Count == 0 && session.UnownedAnswersDropped == unowned + 1;
            byte[] modelBytes = { 9, 8, 7, 6, 5 };
            ipc.FeedLineForTest(ModelAnswerLine(modelId, modelFile, modelBytes, true), modelBytes);
            WmvIpcClient.AssetResponse got = modelAnswers.Count > 0 ? modelAnswers[0] : null;
            Check(modelId.StartsWith("u", StringComparison.Ordinal) && flying && stillFlying && modelAnswers.Count == 1 && got.requestId == modelId &&
                  got.ok && got.data == modelBytes && got.role == "" && ipc.Cache.Holds(modelFile) && !ipc.Cache.InFlight(modelFile),
                  "terrain routing: a role-carrying answer under the model request " + modelId + " goes to the map handler (dropped as unowned) " +
                  "and leaves that request's cache flight out; the request's own answer still reaches OnAssetResponse and the cache", log);

            // ---- a map-namespace answer carrying NO role: the id alone makes it a map answer -- the map handler's (unowned), never
            // OnAssetResponse or the cache
            int mapBefore = mapAnswers.Count, routedBefore = ipc.MapAnswersRouted, modelBefore = modelAnswers.Count;
            unowned = session.UnownedAnswersDropped;
            ipc.FeedLineForTest("{\"type\":\"assetResponse\",\"requestId\":\"map77\",\"ok\":false,\"fileDataID\":" + TerrainTestWdt + ",\"load\":503," +
                                "\"assetEpoch\":7,\"state\":\"stale\",\"detail\":\"x\",\"error\":\"x\"}", null);
            WmvIpcClient.AssetResponse roleless = mapAnswers.Count > mapBefore ? mapAnswers[mapAnswers.Count - 1] : null;
            Check(mapAnswers.Count == mapBefore + 1 && ipc.MapAnswersRouted == routedBefore + 1 && session.UnownedAnswersDropped == unowned + 1 &&
                  modelAnswers.Count == modelBefore && roleless != null && roleless.requestId == "map77" && string.IsNullOrEmpty(roleless.role),
                  "terrain routing: a map-namespace answer with no role (map77) goes by its id alone to OnMapAssetResponse (" +
                  (mapAnswers.Count - mapBefore) + ", routed " + routedBefore + " -> " + ipc.MapAnswersRouted + ") and is dropped there as unowned; " +
                  "OnAssetResponse gets nothing (" + (modelAnswers.Count - modelBefore) + ")", log);

            // ---- an ordinary model answer (not cached) is unaffected
            string plainId = ipc.RequestAssetByFileDataID(990303, false);
            ipc.FeedLineForTest(ModelAnswerLine(plainId, 990303, modelBytes, false), modelBytes);
            Check(modelAnswers.Count == 2 && modelAnswers[1].requestId == plainId && mapAnswers.Count == 6 && ipc.MapAnswersRouted == 6,
                  "terrain routing: an ordinary model answer (" + plainId + ") still goes to OnAssetResponse; " + ipc.MapAnswersRouted +
                  " map answers routed in all, all to the map handler", log);

            // ---- no map handler wired: the answer is dropped and counted, never handed to OnAssetResponse
            ipc.OnMapAssetResponse = null;
            int routed = ipc.MapAnswersRouted;
            ipc.FeedLineForTest(MapAnswerLine("map99", TerrainTestWdt, "wdt", 504, wdt), wdt);
            Check(ipc.MapAnswersUnhandled == 1 && ipc.MapAnswersRouted == routed + 1 && modelAnswers.Count == 2 && mapAnswers.Count == 6,
                  "terrain routing: a map answer with no OnMapAssetResponse wired is dropped (unhandled " + ipc.MapAnswersUnhandled +
                  "), not handed to OnAssetResponse", log);
        }
        finally
        {
            if (session != null)
                session.Dispose();
            UnityEngine.Object.DestroyImmediate(go);
        }
        Check(WmvRuntimeTerrainTile.Live == liveBefore && TerrainTallies() == "0/0/0",
              "terrain routing: every tile these tests built is released (live " + WmvRuntimeTerrainTile.Live + ")", log);
    }

    static string TerrainLoadLine(int load, int epoch)
    {
        return "{\"type\":\"loadTerrainTile\",\"load\":" + load + ",\"assetEpoch\":" + epoch + ",\"mapId\":0,\"mapDirectory\":\"Azeroth\"," +
               "\"product\":\"test\",\"build\":\"0\",\"wdtFileDataID\":" + TerrainTestWdt + ",\"wdtResolvedBy\":\"fileDataID\",\"tileX\":" +
               TerrainTestTileX + ",\"tileY\":" + TerrainTestTileY + ",\"maxAssetBytes\":4194304}";
    }

    static string RefusedMapAnswerLine(string requestId, int fileDataID, string role, int load, string state, string detail)
    {
        return "{\"type\":\"assetResponse\",\"requestId\":\"" + requestId + "\",\"ok\":false,\"fileDataID\":" + fileDataID + ",\"role\":\"" + role +
               "\",\"load\":" + load + ",\"assetEpoch\":7,\"state\":\"" + state + "\",\"detail\":\"" + detail + "\",\"readMs\":0.25,\"error\":\"" +
               detail + "\"}";
    }

    static string ModelAnswerLine(string requestId, int fileDataID, byte[] data, bool cacheable)
    {
        return "{\"type\":\"assetResponse\",\"requestId\":\"" + requestId + "\",\"ok\":true,\"path\":\"\",\"fileDataID\":" + fileDataID +
               ",\"byteLength\":" + data.Length + ",\"encoding\":\"binary\",\"payloadBytes\":" + data.Length +
               (cacheable ? ",\"cacheable\":true" : "") + "}";
    }

    /// <summary>The last line sent of a message type, or "".</summary>
    static string LastSent(List<string> sent, string type)
    {
        string prefix = "{\"type\":\"" + type + "\"";
        for (int i = sent.Count - 1; i >= 0; i--)
            if (sent[i].StartsWith(prefix, StringComparison.Ordinal)) return sent[i];
        return "";
    }

    static int CountSent(List<string> sent, string prefix)
    {
        int n = 0;
        foreach (string line in sent)
            if (line.StartsWith(prefix, StringComparison.Ordinal)) n++;
        return n;
    }

    static bool SentContains(List<string> sent, string prefix) { return CountSent(sent, prefix) > 0; }

    /// <summary>Why the tests that drive a WmvMain of their own cannot run now, or null: a capture or a debug switch is set,
    /// and these paths would act on it (a coroutine, a dump, a camera override).</summary>
    static string TerrainMainTestsBlocked()
    {
        if (!string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WMV_VIEWPORT_SHOT")) ||
            !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WMV_VIEWPORT_ORBIT")) || WmvModelBuilder.Debug_.LightCheck ||
            WmvModelBuilder.Debug_.AllocCheck || WmvModelBuilder.Debug_.HasFrameBounds || WmvTerrainDebug.View != WmvTerrainView.Shaded ||
            WmvTerrainDebug.OnlyChunk >= 0 || WmvTerrainDebug.Focus >= 0 || WmvTerrainDebug.TopDown || WmvTerrainDebug.DumpName != null ||
            WmvTerrainDebug.AnyMaterialDump)
            return "a capture or debug switch is set, and these tests drive a WmvMain that cannot start a coroutine";
        return null;
    }

    /// <summary>
    /// Late map answers through a WmvMain of the test's own (as TerrainHiddenModelTests: an inactive GameObject, so its
    /// Awake never runs; the fields these paths read are set by hand), wired as the player wires it -- WireTerrainMessages
    /// for loadTerrainTile, clearTerrain and the map answers, OnLoadWoWModel to HandleLoadWoWModel, OnAssetResponse to
    /// HandleAssetResponse (counted) -- and fed host lines through the client's own dispatch:
    ///   - a tile builds through that wiring with no answer on the model asset path;
    ///   - a terrain load whose place a model load took: its late WDT answer goes to the session (dropped as late), never to
    ///     HandleAssetResponse, and the model job waiting for its .m2 is exactly as it was -- also when a map answer names
    ///     that job's own request id, and when a map answer is handed to HandleAssetResponse directly (dropped there by its
    ///     guard, counted once for a role and once for a map id without one);
    ///     the job's own .m2 answer then reaches it (parsed, its skin asked for);
    ///   - the same for a world-model job waiting for its root, whose own answer then reaches it (its groups asked for);
    ///   - a map id with no role goes through that wiring to the session too, and runtimeState then sends the session's
    ///     request bookkeeping (retired, retired in all, the last retired, late and unowned apart) under the host's names.
    /// </summary>
    static void TerrainLateAnswerMainTests(Action<string> log)
    {
        string blocked = TerrainMainTestsBlocked();
        if (blocked != null)
        {
            log("terrain late answers: not run -- " + blocked);
            return;
        }
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        const int modelFile = 990401, skinFile = 990402, wmoFile = 990501;
        const uint wmoFirstGroup = 990510, wmoTexture = 990520;
        int modelsLive = WmvRuntimeModel.Live, tilesLive = WmvRuntimeTerrainTile.Live, wmosLive = WmvRuntimeMapObject.Live;
        byte[] wdt = TerrainWdt(true, tx, ty, (int)TerrainTestRoot);
        byte[] root = AdtSynthetic.FlatRoot(tx, ty);
        byte[] m2 = M2Synthetic.InFileSkeletonModel(skinFile);
        byte[] wmoRootBytes = WmoSynthetic.SquaresRoot(3, wmoFirstGroup, wmoTexture);
        var objects = new List<GameObject>();
        var ipcGo = new GameObject("TerrainLateAnswerIpc");
        objects.Add(ipcGo);
        var ipc = ipcGo.AddComponent<WmvIpcClient>();
        var sent = new List<string>();
        ipc.SentForTest = s => sent.Add(s);
        WmvMain main = null;
        WmvTerrainSession session = null;
        var reports = new List<WmvIpcClient.TerrainTileReport>();
        int assetPathCalls = 0;
        Func<int, int> reportsFor = load => reports.FindAll(r => r.Load == load).Count;
        Func<object, string, object> field = (o, name) => o.GetType().GetField(name, BindingFlags.Instance | BindingFlags.Public).GetValue(o);
        try
        {
            var mainGo = new GameObject("TerrainLateAnswerMain");
            objects.Add(mainGo);
            mainGo.SetActive(false);                                          // before AddComponent: no Awake
            main = mainGo.AddComponent<WmvMain>();
            GameObject camGo;
            WmvOrbitCamera orbit = NewCamera(out camGo);
            objects.Add(camGo);
            Camera cam = camGo.GetComponent<Camera>();
            cam.enabled = false;
            cam.aspect = 1f;
            cam.cullingMask = WmvTerrainBuilder.ModelViewMask;
            var mode = new WmvTerrainViewMode(orbit, cam);
            SetMainField(main, "ipc", ipc);
            SetMainField(main, "status", mainGo.AddComponent<WmvStatusOverlay>());
            SetMainField(main, "orbit", orbit);
            SetMainField(main, "terrainViewMode", mode);
            var view = (IWmvTerrainView)Activator.CreateInstance(typeof(WmvMain).GetNestedType("MainTerrainView", BindingFlags.NonPublic),
                                                            new object[] { main });
            session = new WmvTerrainSession(view, ipc.RequestMapAsset, r => reports.Add(r), null, a => a());
            SetMainField(main, "terrain", session);
            InvokeMain(main, "WireTerrainMessages");
            WmvMain m = main;
            ipc.OnAssetResponse = r => { assetPathCalls++; InvokeMain(m, "HandleAssetResponse", r); };
            ipc.OnLoadWoWModel = (path, fileDataID, client, character, load, kind, keepView) =>
                InvokeMain(m, "HandleLoadWoWModel", path, fileDataID, client, character, load, kind, keepView);
            Func<int, string, string> modelLoadLine = (fileDataID, kind) =>
                "{\"type\":\"loadWoWModel\",\"path\":\"\",\"fileDataID\":" + fileDataID + ",\"client\":\"test\",\"character\":false,\"load\":9," +
                "\"kind\":\"" + kind + "\",\"assetEpoch\":7}";

            // ---- a tile through the player's own wiring: both answers go to the session, none to the model asset path
            ipc.FeedLineForTest(TerrainLoadLine(401, 7), null);
            string wdtId = RequestIdOf(LastSent(sent, "getMapAsset"));
            ipc.FeedLineForTest(MapAnswerLine(wdtId, TerrainTestWdt, "wdt", 401, wdt), wdt);
            ipc.FeedLineForTest(MapAnswerLine(RequestIdOf(LastSent(sent, "getMapAsset")), (int)TerrainTestRoot, "rootAdt", 401, root), root);
            for (int f = 0; f < 3; f++)
                session.Pump(4000.0 + f);
            WmvIpcClient.TerrainTileReport built = reports.Find(r => r.Load == 401);
            Check(built != null && built.Status == "built" && reportsFor(401) == 1 && assetPathCalls == 0 && mode.Active,
                  "terrain late answers: a tile builds through the player's wiring (WireTerrainMessages), its answers never on the model asset " +
                  "path (" + assetPathCalls + " there; tile " + (built != null ? built.Status : "not reported") + ")", log);

            // ---- a model load takes a terrain load's place; the terrain load's late answer is the session's alone
            ipc.FeedLineForTest(TerrainLoadLine(402, 7), null);
            string id402 = RequestIdOf(LastSent(sent, "getMapAsset"));
            ipc.FeedLineForTest(modelLoadLine(modelFile, "m2"), null);
            object job = GetMainField(main, "job");
            string pendingM2 = job != null ? (string)field(job, "PendingM2") : null;
            WmvIpcClient.TerrainTileReport r402 = reports.Find(r => r.Load == 402);
            Func<bool> jobUntouched = () => GetMainField(m, "job") == job && job != null && (string)field(job, "PendingM2") == pendingM2 &&
                                            field(job, "M2Bytes") == null && field(job, "Model") == null;
            int late = session.LateAnswersDropped;
            ipc.FeedLineForTest(MapAnswerLine(id402, TerrainTestWdt, "wdt", 402, wdt), wdt);
            Check(r402 != null && r402.Status == "superseded" && reportsFor(402) == 1 && pendingM2 != null && session.LateAnswersDropped == late + 1 &&
                  assetPathCalls == 0 && jobUntouched() && !mode.Active,
                  "terrain late answers: a terrain load's WDT answer arriving after a model load took its place goes to the session (dropped as " +
                  "late), never to HandleAssetResponse (" + assetPathCalls + " call(s)); the model job waiting for " + pendingM2 + " is as it was", log);

            // ---- a map answer naming the model job's own request id: still the session's (unowned), the job untouched
            int unowned = session.UnownedAnswersDropped;
            ipc.FeedLineForTest(MapAnswerLine(pendingM2, (int)TerrainTestRoot, "rootAdt", 402, wdt), wdt);
            Check(session.UnownedAnswersDropped == unowned + 1 && assetPathCalls == 0 && jobUntouched(),
                  "terrain late answers: a map answer (role rootAdt) under the model job's own request id " + pendingM2 + " goes to the session, " +
                  "not to the job, which is untouched", log);

            // ---- defence in depth: HandleAssetResponse handed a map answer directly drops it before any model path, by each half
            // of the rule -- its guard's own count says it acted on both (a map id with no role would otherwise reach the
            // model paths unseen: no job waits for a "map" id)
            int guardBefore = main.MapAnswersDroppedOnAssetPath;
            InvokeMain(main, "HandleAssetResponse", HiddenMapAnswer(pendingM2, modelFile, "wdt", 402, wdt));
            int guardByRole = main.MapAnswersDroppedOnAssetPath - guardBefore;
            InvokeMain(main, "HandleAssetResponse", new WmvIpcClient.AssetResponse
            {
                requestId = id402, ok = false, error = "test: a map id with no role", role = "", path = "", state = "", detail = "", md5 = "",
            });
            int guardById = main.MapAnswersDroppedOnAssetPath - guardBefore - guardByRole;
            Check(guardByRole == 1 && guardById == 1 && main.MapAnswersDroppedOnAssetPath == guardBefore + 2 && guardBefore == 0 && jobUntouched(),
                  "terrain late answers: HandleAssetResponse itself drops a map answer handed to it -- by role under the job's own id (" +
                  guardByRole + "), by a map id (" + id402 + ") without a role (" + guardById + "); its guard counted " + guardBefore + " -> " +
                  main.MapAnswersDroppedOnAssetPath + " -- and the model job is untouched", log);

            // ---- the job's own answer still reaches it
            int requestsBefore = sent.Count;
            ipc.FeedLineForTest(ModelAnswerLine(pendingM2, modelFile, m2, false), m2);
            object jobAfter = GetMainField(main, "job");
            string pendingSkin = jobAfter != null ? (string)field(jobAfter, "PendingSkin") : null;
            Check(assetPathCalls == 1 && jobAfter == job && field(job, "M2Bytes") == m2 && field(job, "Model") != null && pendingSkin != null &&
                  SentContains(sent.GetRange(requestsBefore, sent.Count - requestsBefore), "{\"type\":\"getAssetByFileDataID\",\"requestId\":\"" + pendingSkin + "\""),
                  "terrain late answers: the model job's own .m2 answer reaches HandleAssetResponse (" + assetPathCalls + " call) and the job: " +
                  "parsed, its skin asked for (" + (pendingSkin ?? "nothing") + ")", log);

            // ---- the same for a world-model job waiting for its root
            ipc.FeedLineForTest(TerrainLoadLine(403, 7), null);                // drops the model job
            string id403 = RequestIdOf(LastSent(sent, "getMapAsset"));
            ipc.FeedLineForTest(modelLoadLine(wmoFile, "wmo"), null);
            object wmoJob = GetMainField(main, "wmoJob");
            string pendingRoot = wmoJob != null ? (string)field(wmoJob, "PendingRoot") : null;
            WmvIpcClient.TerrainTileReport r403 = reports.Find(r => r.Load == 403);
            Func<bool> wmoUntouched = () => GetMainField(m, "wmoJob") == wmoJob && wmoJob != null && (string)field(wmoJob, "PendingRoot") == pendingRoot &&
                                            field(wmoJob, "Root") == null;
            int calls = assetPathCalls;
            late = session.LateAnswersDropped;
            ipc.FeedLineForTest(MapAnswerLine(id403, TerrainTestWdt, "wdt", 403, wdt), wdt);
            ipc.FeedLineForTest(MapAnswerLine(pendingRoot, (int)TerrainTestRoot, "rootAdt", 403, wdt), wdt);
            bool lateKept = r403 != null && r403.Status == "superseded" && reportsFor(403) == 1 && session.LateAnswersDropped == late + 1 &&
                            assetPathCalls == calls && pendingRoot != null && wmoUntouched() && GetMainField(main, "job") == null;
            requestsBefore = sent.Count;
            ipc.FeedLineForTest(ModelAnswerLine(pendingRoot, wmoFile, wmoRootBytes, false), wmoRootBytes);
            Check(lateKept && assetPathCalls == calls + 1 && GetMainField(main, "wmoJob") == wmoJob && field(wmoJob, "Root") != null &&
                  field(wmoJob, "PendingRoot") == null &&
                  SentContains(sent.GetRange(requestsBefore, sent.Count - requestsBefore), "{\"type\":\"getAssetByFileDataID\""),
                  "terrain late answers: a world-model job waiting for its root (" + pendingRoot + ") sees neither the late terrain answer nor a " +
                  "map answer under its own id; its root's answer reaches it (parsed, its groups asked for)", log);
            Check(session.PendingRequests == 0 && session.RetiredRequests == 2 && session.ReportsSent == 3 && session.SecondReportsRefused == 0 &&
                  session.RetiredTotal == 2 && session.LastRetiredRequest == id403,
                  "terrain late answers: three terrain loads, three reports; nothing pending, the two ended loads' requests retired (" +
                  session.RetiredTotal + " in all, the last " + session.LastRetiredRequest + ")", log);

            // ---- a map id with no role through the player's own wiring: the session's (unowned), never the model asset path;
            // then runtimeState (FillTerrainRuntimeState) sends the session's bookkeeping under the host's names, late and
            // unowned apart -- here they differ (2 and 3), so a swap shows
            int callsBefore = assetPathCalls, unownedBefore = session.UnownedAnswersDropped;
            ipc.FeedLineForTest("{\"type\":\"assetResponse\",\"requestId\":\"map900\",\"ok\":false,\"fileDataID\":" + TerrainTestWdt + ",\"load\":403," +
                                "\"assetEpoch\":7,\"state\":\"stale\",\"detail\":\"x\",\"error\":\"x\"}", null);
            int stateFrom = sent.Count;
            InvokeMain(main, "HandleRuntimeState", 31);
            string stateLine = LastSent(sent.GetRange(stateFrom, sent.Count - stateFrom), "runtimeState");
            int lateNow = session.LateAnswersDropped, unownedNow = session.UnownedAnswersDropped;
            string bookkeeping = "\"terrainPendingRequests\":0,\"terrainRetiredRequests\":2,\"terrainRetiredTotal\":2,\"terrainLastRetiredRequest\":\"" +
                                 id403 + "\",\"mapAnswersRouted\":" + ipc.MapAnswersRouted + ",\"mapAnswersDropped\":" + (lateNow + unownedNow) +
                                 ",\"mapAnswersLate\":" + lateNow + ",\"mapAnswersUnowned\":" + unownedNow + ",";
            Check(assetPathCalls == callsBefore && unownedNow == unownedBefore + 1 && lateNow == 2 && unownedNow == 3 &&
                  stateLine.Contains(bookkeeping) && main.MapAnswersDroppedOnAssetPath == 2,
                  "terrain late answers: a map id with no role (map900) fed through the player's wiring goes to the session (unowned " +
                  unownedBefore + " -> " + unownedNow + "), not to the model asset path (" + (assetPathCalls - callsBefore) + "); runtimeState " +
                  "then sends " + bookkeeping + " (" + (stateLine.Length > 0 ? "sent" : "nothing sent") + ")", log);
            InvokeMain(main, "SupersedeMapObjectJob", "test end");
        }
        catch (Exception e)
        {
            // A path that threw (through reflection, the cause is the inner exception) fails here instead of ending the run.
            Exception cause = e is TargetInvocationException && e.InnerException != null ? e.InnerException : e;
            Check(false, "terrain late answers: the test threw " + cause.GetType().Name + ": " + cause.Message + " at " + cause.StackTrace, log);
        }
        finally
        {
            if (main != null)
                InvokeMain(main, "DisposeTerrain");
            foreach (GameObject go in objects)
                if (go != null) UnityEngine.Object.DestroyImmediate(go);
        }
        Check(WmvRuntimeModel.Live == modelsLive && WmvRuntimeTerrainTile.Live == tilesLive && WmvRuntimeMapObject.Live == wmosLive &&
              TerrainTallies() == "0/0/0",
              "terrain late answers: every runtime these tests made is released (models " + WmvRuntimeModel.Live + ", tiles " +
              WmvRuntimeTerrainTile.Live + ", world models " + WmvRuntimeMapObject.Live + ")", log);
    }

    /// <summary>
    /// The character on screen through terrain tiles, in a WmvMain of the test's own, driving the player's terrain wiring
    /// itself (HandleLoadTerrainTile, HandleAssetResponse, MainTerrainView, SetViewMode, FrameRiddenScene,
    /// LeaveTerrainForModelLoad, HandleRuntimeState). That WmvMain is added to an INACTIVE GameObject, so its Awake (the
    /// player's set-up, this self-test) never runs; the fields these paths read are set by hand.
    ///   - Design 5.3: a terrain load drops the loads in flight, never the work of the character on screen -- a scene
    ///     revision its dresser is still preparing and a mount being prepared for it survive the load, nothing is answered
    ///     "superseded" for them, and the revision is applied when its file arrives while the tile is shown, the camera
    ///     left on the tile.
    ///   - A mount change while the terrain view holds the camera is neither fitted nor counted (viewFramings); leaving the
    ///     view by clearTerrain does the fit and counts it once; leaving it for a model load drops it (that load frames).
    ///   - runtimeState's terrainMeshes, terrainMaterials and terrainGameObjects under the host's names: the tile's own
    ///     objects while it is shown, 0 at once after clearTerrain and after a model load -- while Unity, whose
    ///     Object.Destroy waits for the end of the frame, still counts the tile's Meshes.
    /// Runs after TerrainTests: the tiles go through the player's own deferred Dispose, so Unity's object counts are
    /// not back within this frame.
    /// </summary>
    static void TerrainHiddenModelTests(Action<string> log)
    {
        string blocked = TerrainMainTestsBlocked();
        if (blocked != null)
        {
            log("terrain hidden model: not run -- " + blocked);
            return;
        }
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        const int skinId = 990201, itemId = 990202, otherItemId = 990203, mountId = 990210;
        int modelsLive = WmvRuntimeModel.Live, tilesLive = WmvRuntimeTerrainTile.Live;
        byte[] wdt = TerrainWdt(true, tx, ty, (int)TerrainTestRoot);
        byte[] root = AdtSynthetic.FlatRoot(tx, ty);
        var objects = new List<GameObject>();
        var ipcGo = new GameObject("TerrainHiddenModelIpc");
        objects.Add(ipcGo);
        var ipc = ipcGo.AddComponent<WmvIpcClient>();
        var sent = new List<string>();
        ipc.SentForTest = s => sent.Add(s);
        byte[] itemBytes = M2Synthetic.InFileSkeletonModel(skinId);
        M2ParsedModel bodyModel = M2Parser.Parse(itemBytes, 0);
        WmvRuntimeModel body = WmvModelBuilder.Build(bodyModel, M2SkinParser.Parse(M2Synthetic.TransformSwitchSkin()),
                                                     new Dictionary<int, BlpImage>(), "TerrainHiddenBody", s => { });
        var dresser = new WmvCharacterDresser(ipc, 17, hash => null, s => { });
        WmvMountedScene mounted = null;
        WmvMain main = null;
        var asked = new List<string[]>();                                  // requestId, role
        var reports = new List<WmvIpcClient.TerrainTileReport>();
        Func<int, string> runtimeState = query =>
        {
            int from = sent.Count;
            InvokeMain(main, "HandleRuntimeState", query);
            for (int i = sent.Count - 1; i >= from; i--)
                if (sent[i].StartsWith("{\"type\":\"runtimeState\"", StringComparison.Ordinal)) return sent[i];
            return "";
        };
        Func<string, string, string> tail = (line, key) =>
        {
            int at = line.IndexOf(",\"" + key + "\":", StringComparison.Ordinal);
            return at < 0 ? "absent" : line.Substring(at + 1);
        };
        try
        {
            var files = (Dictionary<int, byte[]>)typeof(WmvCharacterDresser).GetField("files", BindingFlags.Instance | BindingFlags.NonPublic)
                                                                            .GetValue(dresser);
            files[skinId] = M2Synthetic.TransformSwitchSkin();
            files[itemId] = itemBytes;
            dresser.BeginAttachments(body, bodyModel, 4198151, HiddenModelScene(1, itemId));
            dresser.Retarget(HiddenModelScene(2, otherItemId));               // its .m2 still on its way
            bool revisionPending = dresser.Busy && dresser.AppliedRevision == 1;
            mounted = new WmvMountedScene(ipc.RequestAssetByFileDataID, s => { }, () => { });
            mounted.Retarget(new WmvIpcClient.SceneMount { key = "M1", fileDataID = mountId, sequenceIndex = 0, riderSequenceIndex = 0, bone = -1 }, 1.0);
            bool mountPending = mounted.PreparingFileDataID == mountId;

            var mainGo = new GameObject("TerrainHiddenModelMain");
            objects.Add(mainGo);
            mainGo.SetActive(false);                                          // before AddComponent: no Awake
            main = mainGo.AddComponent<WmvMain>();
            GameObject camGo;
            WmvOrbitCamera orbit = NewCamera(out camGo);
            objects.Add(camGo);
            Camera cam = camGo.GetComponent<Camera>();
            cam.enabled = false;
            cam.aspect = 1f;
            cam.cullingMask = WmvTerrainBuilder.ModelViewMask;
            var mode = new WmvTerrainViewMode(orbit, cam);
            SetMainField(main, "ipc", ipc);
            SetMainField(main, "status", mainGo.AddComponent<WmvStatusOverlay>());
            SetMainField(main, "orbit", orbit);
            SetMainField(main, "dresser", dresser);
            SetMainField(main, "mounted", mounted);
            SetMainField(main, "terrainViewMode", mode);
            var view = (IWmvTerrainView)Activator.CreateInstance(typeof(WmvMain).GetNestedType("MainTerrainView", BindingFlags.NonPublic),
                                                            new object[] { main });
            var session = new WmvTerrainSession(view, (role, id, load, epoch) =>
            {
                string r = "t" + (asked.Count + 1);
                asked.Add(new[] { r, role });
                return r;
            }, r => reports.Add(r), null, a => a());
            SetMainField(main, "terrain", session);
            Action<int> buildTile = load =>
            {
                InvokeMain(main, "HandleLoadTerrainTile", TerrainCommand(load, tx, ty));
                InvokeMain(main, "HandleMapAssetResponse", HiddenMapAnswer(asked[asked.Count - 1][0], TerrainTestWdt, "wdt", load, wdt));
                InvokeMain(main, "HandleMapAssetResponse", HiddenMapAnswer(asked[asked.Count - 1][0], (int)TerrainTestRoot, "rootAdt", load, root));
                for (int f = 0; f < 3; f++)
                    session.Pump(1000.0 * load + f);
            };
            // The model view before any tile: framed on a box that is not the character's, so a fit to the character shows.
            Bounds bodyBox = WmvMountedScene.WorldBounds(body.Bounds, body.Root.transform.localToWorldMatrix);
            orbit.Frame(new Bounds(bodyBox.center + new Vector3(5f, 0f, 0f), bodyBox.size * 3f));

            // ---- design 5.3: the terrain load keeps the work of the character on screen
            int sentBefore = sent.Count;
            InvokeMain(main, "HandleLoadTerrainTile", TerrainCommand(5, tx, ty));
            bool nothingAnswered = !SentSince(sent, sentBefore, "\"type\":\"characterSceneApplied\"");
            bool workKept = dresser.Busy && dresser.AppliedRevision == 1 && mounted.PreparingFileDataID == mountId && session.Loading;
            InvokeMain(main, "HandleMapAssetResponse", HiddenMapAnswer(asked[asked.Count - 1][0], TerrainTestWdt, "wdt", 5, wdt));
            InvokeMain(main, "HandleMapAssetResponse", HiddenMapAnswer(asked[asked.Count - 1][0], (int)TerrainTestRoot, "rootAdt", 5, root));
            for (int f = 0; f < 3; f++)
                session.Pump(5000.0 + f);
            WmvIpcClient.TerrainTileReport built5 = reports.Find(r => r.Load == 5);
            bool tileShown = built5 != null && built5.Status == "built" && mode.Active && cam.cullingMask == WmvTerrainBuilder.TerrainViewMask;
            WmvOrbitCamera.State onTile = orbit.Snapshot();
            string itemRequest = DresserRequestFor(dresser, otherItemId);
            InvokeMain(main, "HandleAssetResponse", new WmvIpcClient.AssetResponse
            {
                requestId = itemRequest, ok = true, fileDataID = otherItemId, data = itemBytes, byteLength = itemBytes.Length, error = "", path = "",
            });
            bool applied = dresser.AppliedRevision == 2 && SentSince(sent, sentBefore, "\"revision\":2,\"status\":\"applied\"") &&
                           !SentSince(sent, sentBefore, "\"status\":\"superseded\"") && orbit.Snapshot().SameAs(onTile);
            Check(revisionPending && mountPending && nothingAnswered && workKept && tileShown && applied,
                  "terrain hidden model: a terrain load keeps the work of the character on screen -- its scene revision 2 still being prepared " +
                  "and the mount being prepared for it survive the load (" + (workKept ? "kept" : "dropped") + "), nothing is answered " +
                  "\"superseded\" for them, and revision 2 is applied when its file arrives while the tile is shown, the camera left on the " +
                  "tile (" + (applied ? "applied" : "not applied, revision " + dresser.AppliedRevision) + "; tile " +
                  (built5 != null ? built5.Status : "not reported") + ")", log);

            // ---- runtimeState with the tile shown: the tiles' own objects, under the host's names
            string shownLine = runtimeState(21);
            Check(shownLine.Contains("\"liveTerrainTiles\":1,") && shownLine.Contains("\"terrainView\":true,") &&
                  shownLine.Contains(",\"terrainMeshes\":256,\"terrainMaterials\":1,\"terrainGameObjects\":257," + NoTerrainRequests),
                  "terrain hidden model: runtimeState with the tile shown sends the tile's own objects -- " + tail(shownLine, "terrainMeshes"), log);

            // ---- a mount change in the terrain view: kept, not counted; clearTerrain leaves the view and does it, once
            mounted.Dispose();
            mounted = null;
            SetMainField(main, "mounted", null);                              // it rides nothing: framed on its own box
            int framingsOnTile = main.ViewFramings;
            InvokeMain(main, "FrameRiddenScene", "test: a mount went on");
            bool held = main.ViewFramings == framingsOnTile && orbit.Snapshot().SameAs(onTile);
            int meshesWithTile = Meshes();
            session.Clear("test");                                            // the player's own Dispose: deferred
            string clearedLine = runtimeState(22);
            WmvOrbitCamera.State afterClear = orbit.Snapshot();
            bool fitted = main.ViewFramings == framingsOnTile + 1 && afterClear.Pivot == bodyBox.center &&
                          afterClear.Yaw == WmvOrbitCamera.FrontYaw && afterClear.Pitch == WmvOrbitCamera.DefaultPitch && !afterClear.MapObjectPlanes &&
                          !mode.Active && cam.cullingMask == WmvTerrainBuilder.ModelViewMask &&
                          (Bounds)GetMainField(main, "lastFramed") == bodyBox && !(bool)GetMainField(main, "haveRiddenFitPending");
            Check(held && fitted,
                  "terrain hidden model: a mount change while the terrain view holds the camera is not fitted or counted (" +
                  (held ? "held" : "moved") + "); clearTerrain leaves the view and does that fit, counted once (viewFramings " +
                  framingsOnTile + " -> " + main.ViewFramings + ", pivot " + afterClear.Pivot + ", the character's box centre " +
                  bodyBox.center + ")", log);
            Check(clearedLine.Contains("\"liveTerrainTiles\":0,") && clearedLine.Contains("\"terrainView\":false,") &&
                  clearedLine.Contains("\"meshObjects\":" + meshesWithTile + ",") && Meshes() == meshesWithTile &&
                  clearedLine.Contains(",\"terrainMeshes\":0,\"terrainMaterials\":0,\"terrainGameObjects\":0," + NoTerrainRequests),
                  "terrain hidden model: runtimeState right after clearTerrain sends no terrain object -- " + tail(clearedLine, "terrainMeshes") +
                  " -- while Unity, its Object.Destroy deferred to the end of the frame, still counts " + meshesWithTile + " Mesh objects", log);

            // ---- a mount change in the terrain view, then a model load: the fit is dropped (that load frames what it shows)
            buildTile(6);
            WmvIpcClient.TerrainTileReport built6 = reports.Find(r => r.Load == 6);
            WmvOrbitCamera.State modelView = mode.Saved;
            int framingsBefore = main.ViewFramings;
            InvokeMain(main, "FrameRiddenScene", "test: another mount went on");
            InvokeMain(main, "LeaveTerrainForModelLoad");
            string modelLine = runtimeState(23);
            bool dropped = built6 != null && built6.Status == "built" && main.ViewFramings == framingsBefore && orbit.Snapshot().SameAs(modelView) &&
                           (bool)GetMainField(main, "framingOwed") && !(bool)GetMainField(main, "haveRiddenFitPending") && !mode.Active &&
                           cam.cullingMask == WmvTerrainBuilder.ModelViewMask;
            Check(dropped && modelLine.Contains("\"liveTerrainTiles\":0,") &&
                  modelLine.Contains(",\"terrainMeshes\":0,\"terrainMaterials\":0,\"terrainGameObjects\":0," + NoTerrainRequests),
                  "terrain hidden model: leaving the terrain view for a model load drops the kept fit -- the model view as it was, viewFramings " +
                  framingsBefore + " -> " + main.ViewFramings + ", the load owed a framing -- and runtimeState sends no terrain object (" +
                  tail(modelLine, "terrainMeshes") + ")", log);

            // ---- outside the terrain view a mount change is fitted and counted at once, as before
            int framingsModel = main.ViewFramings;
            InvokeMain(main, "FrameRiddenScene", "test: a mount went on in the model view");
            Check(main.ViewFramings == framingsModel + 1 && orbit.Snapshot().Pivot == bodyBox.center,
                  "terrain hidden model: in the model view a mount change is fitted and counted at once (viewFramings " + framingsModel + " -> " +
                  main.ViewFramings + ")", log);
        }
        catch (Exception e)
        {
            // A path that threw (through reflection, the cause is the inner exception) fails here instead of ending the run.
            Exception cause = e is TargetInvocationException && e.InnerException != null ? e.InnerException : e;
            Check(false, "terrain hidden model: the test threw " + cause.GetType().Name + ": " + cause.Message + " at " + cause.StackTrace, log);
        }
        finally
        {
            if (main != null)
                InvokeMain(main, "DisposeTerrain");                         // the session's tiles and the scene root
            if (mounted != null) mounted.Dispose();
            dresser.Dispose();
            body.Dispose();
            foreach (GameObject go in objects)
                if (go != null) UnityEngine.Object.DestroyImmediate(go);
        }
        Check(WmvRuntimeModel.Live == modelsLive && WmvRuntimeTerrainTile.Live == tilesLive && TerrainTallies() == "0/0/0",
              "terrain hidden model: every model runtime and terrain tile these tests made is released (models " + WmvRuntimeModel.Live +
              ", tiles " + WmvRuntimeTerrainTile.Live + ", tallies " + TerrainTallies() + ")", log);
    }

    /// <summary>runtimeState's request bookkeeping with nothing pending or retired, ever.</summary>
    const string NoTerrainRequests = "\"terrainPendingRequests\":0,\"terrainRetiredRequests\":0,\"terrainRetiredTotal\":0,\"terrainLastRetiredRequest\":\"\",";

    static WmvIpcClient.CharacterScene HiddenModelScene(int revision, int itemFileDataID)
    {
        var item = new WmvIpcClient.SceneAttachment { key = "left", fileDataID = itemFileDataID, attachmentId = 2, bone = 2,
                                                      position = new[] { 1f, 2f, 3f }, scale = 1f, visible = true };
        return new WmvIpcClient.CharacterScene { attachmentsOnly = true, load = 17, fileDataID = 4198151, revision = revision,
                                                 attachments = new[] { item } };
    }

    static WmvIpcClient.AssetResponse HiddenMapAnswer(string requestId, int fileDataID, string role, int load, byte[] data)
    {
        return new WmvIpcClient.AssetResponse
        {
            requestId = requestId, ok = true, fileDataID = fileDataID, role = role, load = load, assetEpoch = 7, state = "available", detail = "",
            readMs = 0.25, md5 = WmvTerrainSession.Md5Hex(data), data = data, byteLength = data.Length, error = "", path = "",
        };
    }

    /// <summary>The request id the dresser is waiting on for a file, or null.</summary>
    static string DresserRequestFor(WmvCharacterDresser dresser, int fileDataID)
    {
        var pending = (Dictionary<string, int>)typeof(WmvCharacterDresser).GetField("pending", BindingFlags.Instance | BindingFlags.NonPublic)
                                                                          .GetValue(dresser);
        foreach (var kv in pending)
            if (kv.Value == fileDataID) return kv.Key;
        return null;
    }

    static bool SentSince(List<string> sent, int from, string fragment)
    {
        for (int i = from; i < sent.Count; i++)
            if (sent[i].Contains(fragment)) return true;
        return false;
    }

    static void SetMainField(WmvMain main, string name, object value)
    {
        typeof(WmvMain).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic).SetValue(main, value);
    }

    static object GetMainField(WmvMain main, string name)
    {
        return typeof(WmvMain).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic).GetValue(main);
    }

    static object InvokeMain(WmvMain main, string name, params object[] args)
    {
        return typeof(WmvMain).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic).Invoke(main, args);
    }

    [Serializable] class TerrainLineProbe
    {
        public string type;
        public int load;
        public string status;
    }

    /// <summary>runtimeState read back as JSON: the request bookkeeping fields that are not plain counts in a row.</summary>
    [Serializable] class RuntimeStateProbe
    {
        public int terrainRetiredTotal;
        public string terrainLastRetiredRequest;
    }

    static string RequestIdOf(string line)
    {
        int at = line.IndexOf("\"requestId\":\"", StringComparison.Ordinal);
        if (at < 0) return "";
        at += "\"requestId\":\"".Length;
        int end = line.IndexOf('"', at);
        return end > at ? line.Substring(at, end - at) : "";
    }

    static string MapAnswerLine(string requestId, int fileDataID, string role, int load, byte[] data)
    {
        return "{\"type\":\"assetResponse\",\"requestId\":\"" + requestId + "\",\"ok\":true,\"fileDataID\":" + fileDataID + ",\"role\":\"" + role +
               "\",\"load\":" + load + ",\"assetEpoch\":7,\"state\":\"available\",\"detail\":\"\",\"readMs\":0.25,\"md5\":\"" +
               WmvTerrainSession.Md5Hex(data) + "\",\"byteLength\":" + data.Length + ",\"encoding\":\"binary\",\"payloadBytes\":" + data.Length + "}";
    }
}
