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

public static class WmvLifecycleSelfTest
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
    /// share id 2701"; the host's own display flags can, and they are what the OpenGL viewport draws
    /// from. So: the explicit state decides when it is present, the id rule still decides when it is
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
        ZoomTests(log);
        log(string.Format("lifecycle-test: {0} passed, {1} failed", passed, failed));
    }
}
