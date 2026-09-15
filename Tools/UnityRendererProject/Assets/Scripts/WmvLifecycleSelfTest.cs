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
              rt.MaterialInfo[4].Plan.ProvisionalFallback && rt.MaterialInfo[4].Verdict == "unresolved: U-B2,U-B3,U-B4,U-B5,U-23a,U-E2,U-E3,U-V4",
              "wmo rows: shader 23 with blend 2 keeps its four-layer arithmetic in the labelled opaque fallback, env map unbound " +
              "(U-V4: its one-set group has no MOC2)", log);
        Check(m23Env.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.ProvisionalBaseline && m23Env.GetTexture("_WmoTex0") == null &&
              rt.MaterialInfo[7].Verdict == "unresolved: U-23b",
              "wmo rows: shader 23 without a layer is the baseline drawing the register's white, its env map never bound as a diffuse", log);
        Check(mId5.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.EnvMetal && mId5.GetFloat("_WmoLightBypass") == 0f &&
              mId5.GetTexture("_WmoTex1") == null && rt.MaterialInfo[6].Verdict == "resolved-partial: U-G1,U-E3,U-F1",
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
              rt.MaterialInfo[1].Plan.ProvisionalFallback && rt.MaterialInfo[1].Verdict == "unresolved: U-23b,U-23a,U-E2,U-E3",
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
              rt.MaterialInfo[0].Verdict == "resolved-partial: U-23b,U-23a,U-E2,U-E3,U-V4",
              "wmo 23 rows: a four-layer batch in a group without MOC2 / MOTV sets is logged per group and as U-V4 on its material", log);
        string diag = WmvWmoBuilder.DescribeMaterialDiag(rt.MaterialInfo[0], m, 2, textures);
        Check(diag.Contains("_WmoLayerMask (1,0,1,1)") && diag.Contains("t5 (client t17) height 1") &&
              diag.Contains("RESOLVED-PARTIAL: U-23b,U-23a,U-E2,U-E3,U-V4") && diag.Contains("t0 " + Env + " not bound"),
              "wmo 23 rows: the diagnostic reads back the mask, the client registers and the unbound env map", log);
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
              m7.renderQueue == (int)UnityEngine.Rendering.RenderQueue.Geometry && rt.MaterialInfo[0].Verdict == "resolved-partial: U-G1,U-E3",
              "wmo 7 rows: id 7 blend 1 is permutation 5, t0 on UV0 and t1 on UV1, untested, resolved-partial U-G1,U-E3", log);
        var t0 = m7.GetTexture("_WmoTex0") as Texture2D;
        var t1 = m7.GetTexture("_WmoTex1") as Texture2D;
        Check(t0 != null && t1 != null && !ReferenceEquals(t0, t1) && m7.GetTexture("_WmoTex2") == null,
              "wmo 7 rows: registers 0 and 1 bound to +0x0C and +0x18, the env register t2 left unbound", log);
        Check(fb7.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.TwoLayerEnvMetal && fb7.GetTexture("_WmoTex0") != null &&
              fb7.GetTexture("_WmoTex1") == null && rt.MaterialInfo[1].Plan.ProvisionalFallback &&
              rt.MaterialInfo[1].Verdict == "unresolved: U-23b,U-G1,U-E3",
              "wmo 7 rows: an empty +0x18 is the labelled fallback, its register left white", log);
        Check(m5.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.EnvMetal && m5.GetFloat("_AlphaTest") == 0f &&
              m5.GetFloat("_WmoLightBypass") == 0f && m5.GetTexture("_WmoTex1") == null &&
              rt.MaterialInfo[2].Verdict == "resolved-partial: U-G1,U-E3,U-F1",
              "wmo 5 rows: id 5 blend 1 with F_UNLIT is permutation 6, never clipped, no bypass, env register unbound", log);
        Check(fbBlend.GetFloat("_WmoPermutation") == (float)(int)WmoPermutation.TwoLayerEnvMetal && fbBlend.GetFloat("_AlphaTest") == 0f &&
              fbBlend.GetFloat("_SrcBlend") == (float)UnityEngine.Rendering.BlendMode.One &&
              fbBlend.GetFloat("_DstBlend") == (float)UnityEngine.Rendering.BlendMode.Zero && fbBlend.GetFloat("_ZWrite") == 1f &&
              rt.MaterialInfo[3].Plan.ProvisionalFallback && rt.MaterialInfo[3].Verdict == "unresolved: U-B2,U-B3,U-B4,U-B5,U-G1,U-E3",
              "wmo 7 rows: id 7 blend 3 is the labelled opaque fallback, untested, no factors guessed", log);
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
              diag.Contains("t2 " + Env + " not bound") && diag.Contains("NOT drawn (U-G1, U-E3)") &&
              diag.Contains("| RESOLVED-PARTIAL: U-G1,U-E3"),
              "wmo 7 rows: the diagnostic reads back the permutation, the unbound env map and what is not drawn", log);
        bool summary = false;
        foreach (string line in lines) summary = summary || (line.Contains("wmo built") && line.Contains("4 drawn without the env emissive"));
        Check(summary, "wmo 7/5 rows: the build summary counts the materials drawn without their env emissive", log);
        rt.Dispose();
    }

    /// <summary>
    /// The pixel arithmetic of permutations 2 (four-layer, client case 23) and 3 (two-layer, client case 13)
    /// as the GPU runs it, against the C# reference transcriptions the parser tests pin
    /// (WmoMaterialSemantics.FourLayerWeights / TwoLayerMix). Without it a lerp flipped in WmvWmo.shader, or a
    /// component dropped from the four-layer maximum, would pass every other check.
    ///
    /// A synthetic WMO of three flat quads: a four-layer material with flat-colour layers, height maps of
    /// known alpha and the same MOC2 bytes on every vertex; a two-layer material with the same MOCV set-2
    /// alpha on every vertex; and a plain id-0 reference. Constant vertex streams mean interpolation cannot
    /// move the sample. Each quad is drawn unlit through a diagnostic view (3 effective weights, 4 va, 5 the
    /// combiner diffuse) by an orthographic camera of this test's own into a render texture, and its centre
    /// pixel is compared with the REFERENCE quad drawn through the same view with a flat texture holding the
    /// value the C# function predicts. Comparing against a reference drawn by the same shader, camera and
    /// target, rather than against the bare number, cancels whatever display transform, target encoding or
    /// readback conversion the frame passes through: both pixels take the same path.
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
        byte[][] records =
        {
            WmoSynthetic.Material(0, 23, 0, 0, L1, L2, new uint[] { L3, L4, H1, H2, H3, H4 }),   // 0 four-layer
            WmoSynthetic.Material(0x04, 13, 0, TA, TB),                                       // 1 two-layer
            WmoSynthetic.Material(0x04, 0, 0, TR),                                            // 2 reference
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
            var gs = new WmoSynthetic.GroupSpec
            {
                Flags = WmoGroupFlags.Outdoor | (i == 1 ? WmoGroupFlags.ColorSet2 | WmoGroupFlags.TwoTexCoordSets : 0u),
                Positions = new[] { x, 0f, 0f, x + 10f, 0f, 0f, x + 10f, 10f, 0f, x, 10f, 0f },
                Normals = new[] { 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f },
                Indices = new ushort[] { 0, 1, 2, 0, 2, 3 },
                Batches = new[] { WmoSynthetic.Batch(0, 6, 0, 3, WmoBatch.FlagLargeMaterialId, i) },
                Mpy2 = new byte[] { 0x20, 0, (byte)i, 0, 0x20, 0, (byte)i, 0 },
                Moc2 = i == 0 ? moc2 : null,
            };
            if (i == 1)
                gs.ColorSets.Add(set2);
            int sets = i == 0 ? 4 : i == 1 ? 2 : 1;
            for (int s = 0; s < sets; s++)
                gs.TexCoordSets.Add(new[] { 0f, 0f, 1f, 0f, 1f, 1f, 0f, 1f });
            groups[i] = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(gs), "render group " + i, i);
        }
        WmoRoot root = WmoParser.ParseRoot(WmoSynthetic.BuildRoot(spec), "render root");

        // Layer colours and height alphas. Layer 4 is yellow, so every channel of the mix mixes two layers.
        byte[][] layerRgb = { new byte[] { 255, 0, 0 }, new byte[] { 0, 255, 0 }, new byte[] { 0, 0, 255 }, new byte[] { 255, 255, 0 } };
        byte[] heightAlpha = { 255, 128, 255, 200 };
        byte[] l1 = { 230, 40, 90 }, l2 = { 20, 200, 160 };
        var textures = new Dictionary<uint, WmvWmoTexture>();
        uint[] layerIds = { L1, L2, L3, L4 }, heightIds = { H1, H2, H3, H4 };
        for (int k = 0; k < 4; k++)
        {
            AddRenderTexture(textures, layerIds[k], SolidTexture(layerRgb[k][0], layerRgb[k][1], layerRgb[k][2]));
            AddRenderTexture(textures, heightIds[k], SolidTextureAlpha(90, 90, 90, heightAlpha[k]));
        }
        AddRenderTexture(textures, TA, SolidTexture(l1[0], l1[1], l1[2]));
        AddRenderTexture(textures, TB, SolidTexture(l2[0], l2[1], l2[2]));
        AddRenderTexture(textures, TR, SolidTexture(0, 0, 0));

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

        WmvRuntimeMapObject rt = null;
        GameObject camGo = null;
        RenderTexture target = null;
        RenderTexture prevActive = RenderTexture.active;
        var refs = new List<Texture2D>();
        try
        {
            rt = WmvWmoBuilder.Build(root, groups, textures, "WmoRender", null);
            bool built = rt != null && rt.Materials.Length == 3 && rt.Materials[0] != null && rt.Materials[1] != null && rt.Materials[2] != null;
            Check(built, "wmo render: built", log);
            if (!built) return;
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
