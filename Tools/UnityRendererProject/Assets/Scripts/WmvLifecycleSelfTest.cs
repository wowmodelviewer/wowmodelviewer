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
        MountTests(log);
        ZoomTests(log);
        MapObjectTests(log);
        log(string.Format("lifecycle-test: {0} passed, {1} failed", passed, failed));
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
}
