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

    public static void RunAll(Action<string> log)
    {
        passed = failed = 0;
        foreach (bool skinned in new[] { false, true })
            foreach (int keyed in new[] { 1, 0 })
                Run(skinned, keyed, log);
        GeosetTests(log);
        log(string.Format("lifecycle-test: {0} passed, {1} failed", passed, failed));
    }
}
