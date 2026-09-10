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
        Check(rt.Emitters.LiveParticleCount == 0 && rt.Emitters.RibbonSegmentCount == 0,
              "emitter: the sequence change cleared the previous animation's particles and "
              + "ribbon history (no smear from where the bone used to be)", log);

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

    public static void RunAll(Action<string> log)
    {
        passed = failed = 0;
        foreach (bool skinned in new[] { false, true })
            foreach (int keyed in new[] { 1, 0 })
                Run(skinned, keyed, log);
        GeosetTests(log);
        OutputGateTests(log);
        EmitterTests(log);
        ZoomTests(log);
        log(string.Format("lifecycle-test: {0} passed, {1} failed", passed, failed));
    }
}
