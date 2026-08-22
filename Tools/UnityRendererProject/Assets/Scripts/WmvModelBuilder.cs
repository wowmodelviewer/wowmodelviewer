// WmvModelBuilder.cs
//
// Turns the parsed M2 + skin + decoded textures into a live Unity object: one Mesh with a
// submesh per WoW batch, one Material each, textures uploaded from decoded RGBA bytes.
//
// Everything it creates is tracked so a later load can destroy it: meshes, materials and
// textures are not garbage-collected by Unity on their own, so a viewer that loads model after
// model would leak GPU memory without the explicit Dispose here.

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;

/// <summary>
/// Which textures one material was built from. Kept so the skin can be changed later without
/// rebuilding the mesh: the WMV viewport lets the user pick among a creature's skins (chicken2
/// has seven), and when they do, only these bindings need re-uploading.
/// </summary>
public struct WmvMaterialBinding
{
    public int BaseSlot;    // M2 texture slot behind the main texture, -1 when untextured
    public int EnvSlot;     // M2 texture slot bound as the combiner's second unit, -1 when unused
    public bool DropAlpha;  // was the base texture's alpha discarded on upload? (see CreateTexture)
}

public class WmvRuntimeModel
{
    public GameObject Root;
    public Mesh Mesh;
    public Material[] Materials = new Material[0];
    public Texture2D[] Textures = new Texture2D[0];
    public WmvMaterialBinding[] Bindings = new WmvMaterialBinding[0];

    /// <summary>Geoset number of each submesh, parallel to Materials. 0 = always drawn.</summary>
    public int[] SubmeshGeosets = new int[0];

    /// <summary>Every submesh's triangles, kept so hiding one is a matter of handing the mesh an
    /// empty list and showing it again is handing back this array -- no geometry is re-uploaded
    /// and no material is rebuilt.</summary>
    public int[][] SubmeshTriangles = new int[0][];

    /// <summary>The geoset numbers currently switched on, or null when the host has not said.</summary>
    public HashSet<int> Geosets;
    public Bounds Bounds;
    public int VertexCount, TriangleCount, SubmeshCount;

    /// <summary>Destroy every runtime resource this model owns.</summary>
    public void Dispose()
    {
        if (Root != null) UnityEngine.Object.Destroy(Root);
        if (Mesh != null) UnityEngine.Object.Destroy(Mesh);
        foreach (var m in Materials) if (m != null) UnityEngine.Object.Destroy(m);
        foreach (var t in Textures) if (t != null) UnityEngine.Object.Destroy(t);
        Root = null;
        Mesh = null;
        Materials = new Material[0];
        Textures = new Texture2D[0];
    }
}

public static class WmvModelBuilder
{
    /// <summary>
    /// Diagnostic switches, read from the player command line. They exist to isolate a visual
    /// fault without rebuilding: run the player (or let WMV launch it) with the flag and the
    /// rendering changes accordingly.
    ///
    ///   -wmvFlipV        invert the V texture coordinate (isolates a UV-orientation fault)
    ///   -wmvForceOpaque  force every material opaque, ignoring the WoW blend mode
    ///   -wmvForceSolid   the whole force-solid probe: force-opaque PLUS a fully opaque alpha
    ///                    channel on every uploaded texture, so neither the material state nor
    ///                    the texture can make anything translucent. If the model is STILL
    ///                    see-through with this on, the fault is geometry, depth or winding --
    ///                    not alpha.
    ///   -wmvMatColors    replace textures with a flat per-material colour (isolates a
    ///                    batch/material assignment fault from a texture fault)
    ///   -wmvShowHidden   draw the batches the model hides at rest (see BatchIsVisible). Useful
    ///                    for seeing WHAT is hidden; the hidden geometry usually covers the
    ///                    detail it is meant to replace.
    ///   -wmvOwnShader    resolve the renderer's own WmvOpaque shader before any pipeline
    ///                    shader. The pipeline's Lit shaders cannot run the M2 combiner, so this
    ///                    is how to see the second texture unit in a build where they exist.
    /// </summary>
    public static class Debug_
    {
        static bool parsed;
        static bool flipV, forceOpaque, forceSolid, matColors, showHidden, ownShader;

        static void Parse()
        {
            if (parsed) return;
            parsed = true;
            foreach (var a in System.Environment.GetCommandLineArgs())
            {
                if (a == "-wmvFlipV") flipV = true;
                else if (a == "-wmvForceOpaque") forceOpaque = true;
                else if (a == "-wmvForceSolid") forceSolid = true;
                else if (a == "-wmvMatColors") matColors = true;
                else if (a == "-wmvShowHidden") showHidden = true;
                else if (a == "-wmvOwnShader") ownShader = true;
            }
            if (flipV || forceOpaque || forceSolid || matColors || showHidden || ownShader)
                Debug.Log("WMV debug switches: flipV=" + flipV + " forceOpaque=" + forceOpaque +
                          " forceSolid=" + forceSolid + " matColors=" + matColors +
                          " showHidden=" + showHidden + " ownShader=" + ownShader);
        }

        public static bool FlipV { get { Parse(); return flipV; } }
        public static bool ForceOpaque { get { Parse(); return forceOpaque || forceSolid; } }
        public static bool ForceSolid { get { Parse(); return forceSolid; } }
        public static bool MatColors { get { Parse(); return matColors; } }
        public static bool ShowHidden { get { Parse(); return showHidden; } }
        public static bool OwnShader { get { Parse(); return ownShader; } }
    }

    static readonly Color[] DebugColors =
    {
        Color.red, Color.green, Color.blue, Color.yellow, Color.cyan, Color.magenta,
    };

    /// <summary>
    /// Build a renderable object. decodedTextures maps an M2 texture-slot index to its decoded
    /// pixels; a slot with no entry renders untextured (white), which is what happens for a
    /// replaceable texture the host could not resolve.
    /// </summary>
    /// <summary>
    /// Is this submesh drawn, given the geoset numbers the displayed variant switches on?
    ///
    /// This is the legacy viewport's rule, from WoWModel::setCreatureGeosetData: a submesh whose
    /// geoset number is 0 is always drawn, and every other one is drawn only if the variant names
    /// it. An EMPTY set is an answer, not a blank -- it hides everything but the id-0 submeshes,
    /// which is exactly what the viewport does for a display with no geoset data. A null set means
    /// the host had nothing to say (no creature selection at all), and then nothing is hidden.
    /// </summary>
    public static bool GeosetVisible(int submeshId, HashSet<int> geosets)
    {
        if (geosets == null || submeshId == 0)
            return true;
        return geosets.Contains(submeshId);
    }

    public static WmvRuntimeModel Build(M2ParsedModel model, M2ParsedSkin skin,
                                        Dictionary<int, BlpImage> decodedTextures,
                                        string objectName, Action<string> log,
                                        HashSet<int> geosets = null)
    {
        if (model == null || skin == null)
            throw new WowParseException("builder: nothing to build");
        if (model.Vertices.Length == 0)
            throw new WowParseException("builder: model has no vertices");
        if (skin.Batches.Length == 0)
            throw new WowParseException("builder: skin profile has no draw batches");

        var result = new WmvRuntimeModel();

        // ---- geometry: all model vertices once, converted to Unity space --------------
        int n = model.Vertices.Length;
        var positions = new Vector3[n];
        var normals = new Vector3[n];
        var uvs = new Vector2[n];
        for (int i = 0; i < n; i++)
        {
            float x, y, z;
            WowCoordinateConverter.ConvertPosition(model.Vertices[i].Position, out x, out y, out z);
            positions[i] = new Vector3(x, y, z);
            WowCoordinateConverter.ConvertNormal(model.Vertices[i].Normal, out x, out y, out z);
            normals[i] = new Vector3(x, y, z);
            float u, v;
            WowCoordinateConverter.ConvertTexCoord(model.Vertices[i].TexCoord0, out u, out v);
            if (Debug_.FlipV) v = 1f - v;
            uvs[i] = new Vector2(u, v);
        }

        if (log != null && model.Vertices.Length > 0)
        {
            float u0, v0;
            WowCoordinateConverter.ConvertTexCoord(model.Vertices[0].TexCoord0, out u0, out v0);
            log(string.Format("uv: {0} vertices, set 0 used (M2 carries 2 sets); vertex 0 raw " +
                              "({1:F4},{2:F4}) -> unity ({3:F4},{4:F4}); V flipped by converter" +
                              (Debug_.FlipV ? " and again by -wmvFlipV" : ""),
                              model.Vertices.Length, model.Vertices[0].TexCoord0.X,
                              model.Vertices[0].TexCoord0.Y, uvs[0].x, uvs[0].y));
        }

        var mesh = new Mesh { name = objectName + "_mesh" };
        mesh.indexFormat = n > 65000
            ? UnityEngine.Rendering.IndexFormat.UInt32
            : UnityEngine.Rendering.IndexFormat.UInt16;
        mesh.vertices = positions;
        mesh.normals = normals;
        mesh.uv = uvs;

        // ---- one submesh per batch ----------------------------------------------------
        var materials = new List<Material>();
        var bindings = new List<WmvMaterialBinding>();
        var textures = new List<Texture2D>();
        // Keyed by slot AND by whether the alpha channel was discarded, because the same slot
        // can feed an opaque batch (alpha thrown away) and a blended one (alpha kept).
        var textureCache = new Dictionary<int, Texture2D>();
        var triangleSets = new List<int[]>();
        var submeshGeosets = new List<int>();
        int totalTriangles = 0, hiddenByGeoset = 0;

        // Resolve the shader up front: whether it can run the M2 combiner decides how the base
        // texture's alpha has to be treated, which happens before any material is created.
        FindRenderShader(log);
        bool combinerAvailable = ShaderHasCombiner();

        // Which batches the model actually wants drawn. Decided before the loop so that a model
        // whose every batch is hidden still renders SOMETHING: that is far more likely to be a
        // visibility rule this milestone does not implement than a model that draws nothing.
        var visible = new bool[skin.Batches.Length];
        var hiddenReasons = new string[skin.Batches.Length];
        int hiddenCount = 0;
        for (int i = 0; i < skin.Batches.Length; i++)
        {
            visible[i] = BatchIsVisible(model, skin.Batches[i], out hiddenReasons[i]);
            if (!visible[i]) hiddenCount++;
        }
        bool drawHidden = Debug_.ShowHidden || hiddenCount == skin.Batches.Length;
        if (hiddenCount == skin.Batches.Length && hiddenCount > 0 && log != null)
            log("every batch is hidden at rest -- drawing them all rather than nothing; this is " +
                "almost certainly a visibility rule this milestone does not implement");

        for (int batchIndex = 0; batchIndex < skin.Batches.Length; batchIndex++)
        {
            M2Batch batch = skin.Batches[batchIndex];
            M2MaterialDef mat = (batch.MaterialIndex < model.Materials.Length)
                ? model.Materials[batch.MaterialIndex]
                : default(M2MaterialDef);
            M2BlendMode mode = Debug_.ForceOpaque ? M2BlendMode.Opaque : (M2BlendMode)mat.BlendMode;

            // Does the model actually want this batch drawn right now? See BatchIsVisible.
            if (!visible[batchIndex])
            {
                if (log != null)
                    log(string.Format("batch: submesh {0} is HIDDEN at rest ({1}){2}",
                                      batch.SubmeshIndex, hiddenReasons[batchIndex],
                                      drawHidden ? " -- drawn anyway" : " -- skipped, as the legacy viewport does"));
                if (!drawHidden)
                    continue;
            }

            M2Submesh submesh = skin.Submeshes[batch.SubmeshIndex];
            int[] indices = M2SkinParser.BuildTriangles(skin, submesh, n);
            WowCoordinateConverter.FlipWinding(indices);   // handedness change reverses winding
            triangleSets.Add(indices);
            submeshGeosets.Add(submesh.Id);
            // A geoset the variant does not switch on still gets its submesh and material -- only
            // its triangles are withheld -- so a later variant can switch it back on without
            // rebuilding anything.
            if (GeosetVisible(submesh.Id, geosets))
                totalTriangles += indices.Length / 3;
            else
                hiddenByGeoset++;

            // How this material combines its texture units, resolved exactly as the legacy
            // renderer does. This milestone implements one combiner; the rest are logged and
            // drawn from unit 0 alone, which is what happened to every one of them before.
            var shader = M2ShaderTable.Resolve(batch.TextureCount, batch.ShaderId);
            bool wantsCombiner = batch.TextureCount >= 2 &&
                                 shader.PixelShader == PixelShaderOpaqueMod2xNaAlpha &&
                                 shader.UvSource[1] == M2UvSource.Environment &&
                                 mode == M2BlendMode.Opaque && !Debug_.ForceSolid;
            bool useCombiner = wantsCombiner && combinerAvailable;

            int textureSlot = ResolveTextureSlot(model, batch, 0);
            int envSlot = useCombiner ? ResolveTextureSlot(model, batch, 1) : -1;

            // The base texture's ALPHA is the combiner's mask -- pixel shader 12 folds unit 1 in
            // where alpha is low -- so it must survive when the combiner runs. Discarding it is
            // otherwise still the right call for an opaque material: nothing downstream should be
            // able to read a channel WoW does not treat as transparency.
            bool dropAlpha = Debug_.ForceSolid || (mode == M2BlendMode.Opaque && !useCombiner);

            Texture2D tex = GetTexture(decodedTextures, textureCache, textures, textureSlot,
                                       dropAlpha, false, objectName);
            // Unit 1's own alpha is unused by the combiner; it is sampled by generated sphere-map
            // coordinates, so it must not wrap.
            Texture2D envTex = GetTexture(decodedTextures, textureCache, textures, envSlot,
                                          true, true, objectName);

            if (log != null)
            {
                log(string.Format("batch: submesh {0} ({1} tris) material {2} blend {3} flags 0x{4:X} " +
                                  "twoSided {5} depthWriteOff {6} -> texture slot {7}{8}, alpha {9}",
                                  batch.SubmeshIndex, indices.Length / 3, batch.MaterialIndex,
                                  (M2BlendMode)mat.BlendMode, mat.Flags, mat.TwoSided,
                                  mat.DepthWriteDisabled, textureSlot,
                                  tex == null ? " (no texture)" : "",
                                  dropAlpha ? "forced to 255" : "kept (combiner mask)"));
                log(string.Format("batch: shaderId 0x{0:X4} -> {1} / {2} (combiner {3}), {4} texture unit(s), " +
                                  "unit 1 uv {5}{6}",
                                  batch.ShaderId, shader.PixelShaderName, shader.VertexShaderName,
                                  shader.PixelShader, batch.TextureCount,
                                  batch.TextureCount >= 2 ? shader.UvSource[1].ToString() : "n/a",
                                  useCombiner ? " -> env slot " + envSlot + " bound"
                                              : (batch.TextureCount >= 2
                                                 ? (wantsCombiner
                                                    ? " -- the resolved shader cannot run the combiner, unit 1 dropped"
                                                    : " -- this combiner is not implemented yet, unit 1 dropped")
                                                 : "")));
            }

            bindings.Add(new WmvMaterialBinding
            {
                BaseSlot = textureSlot, EnvSlot = envSlot, DropAlpha = dropAlpha,
            });
            materials.Add(CreateMaterial(mat, mode, tex, envTex,
                                         objectName + "_mat" + materials.Count, log));
        }

        if (hiddenCount > 0 && !drawHidden && log != null)
            log(string.Format("{0} of {1} batch(es) hidden at rest -- pass -wmvShowHidden to draw them",
                              hiddenCount, skin.Batches.Length));

        mesh.subMeshCount = triangleSets.Count;
        for (int i = 0; i < triangleSets.Count; i++)
            mesh.SetTriangles(GeosetVisible(submeshGeosets[i], geosets) ? triangleSets[i] : EmptyTriangles,
                              i, false);
        // Bounds come from the WHOLE model, not from what is currently visible, so switching a
        // variant does not make the camera jump.
        mesh.bounds = BoundsOfAll(positions, triangleSets);

        if (log != null && hiddenByGeoset > 0)
            log(string.Format("geosets: {0} of {1} submesh(es) hidden by the displayed variant [{2}]",
                              hiddenByGeoset, triangleSets.Count, GeosetList(geosets)));

        // ---- scene object -------------------------------------------------------------
        var go = new GameObject(objectName);
        go.AddComponent<MeshFilter>().sharedMesh = mesh;
        var renderer = go.AddComponent<MeshRenderer>();
        renderer.sharedMaterials = materials.ToArray();

        result.Root = go;
        result.Mesh = mesh;
        result.Materials = materials.ToArray();
        result.Bindings = bindings.ToArray();
        result.Textures = textures.ToArray();
        result.SubmeshGeosets = submeshGeosets.ToArray();
        result.SubmeshTriangles = triangleSets.ToArray();
        result.Geosets = geosets;
        result.Bounds = mesh.bounds;
        result.VertexCount = n;
        result.TriangleCount = totalTriangles;
        result.SubmeshCount = triangleSets.Count;
        return result;
    }

    static readonly int[] EmptyTriangles = new int[0];

    /// <summary>Bounds over every triangle the model has, visible or not.</summary>
    static Bounds BoundsOfAll(Vector3[] positions, List<int[]> triangleSets)
    {
        bool any = false;
        Vector3 min = Vector3.zero, max = Vector3.zero;
        foreach (var tris in triangleSets)
        {
            foreach (int i in tris)
            {
                Vector3 p = positions[i];
                if (!any) { min = max = p; any = true; continue; }
                min = new Vector3(Mathf.Min(min.x, p.x), Mathf.Min(min.y, p.y), Mathf.Min(min.z, p.z));
                max = new Vector3(Mathf.Max(max.x, p.x), Mathf.Max(max.y, p.y), Mathf.Max(max.z, p.z));
            }
        }
        var b = new Bounds();
        b.center = (min + max) * 0.5f;
        b.size = max - min;
        return b;
    }

    static string GeosetList(HashSet<int> geosets)
    {
        if (geosets == null) return "not reported";
        if (geosets.Count == 0) return "none";
        var parts = new List<string>();
        foreach (int g in geosets) parts.Add(g.ToString());
        parts.Sort();
        return string.Join(",", parts.ToArray());
    }

    /// <summary>
    /// Switch which geosets the model shows. The mesh, its vertices, its materials and its
    /// textures are all untouched: a variant change only decides which submeshes hand the mesh
    /// their triangles. Returns the number of triangles now drawn.
    /// </summary>
    public static int ApplyGeosets(WmvRuntimeModel runtime, HashSet<int> geosets, Action<string> log)
    {
        if (runtime == null || runtime.Mesh == null ||
            runtime.SubmeshTriangles.Length != runtime.Mesh.subMeshCount)
            return 0;

        runtime.Geosets = geosets;
        int visible = 0, shown = 0, hidden = 0;
        for (int i = 0; i < runtime.SubmeshTriangles.Length; i++)
        {
            bool on = GeosetVisible(runtime.SubmeshGeosets[i], geosets);
            runtime.Mesh.SetTriangles(on ? runtime.SubmeshTriangles[i] : EmptyTriangles, i, false);
            if (on) { visible += runtime.SubmeshTriangles[i].Length / 3; shown++; }
            else hidden++;
        }
        runtime.TriangleCount = visible;
        if (log != null)
            log(string.Format("geosets [{0}]: {1} submesh(es) shown, {2} hidden, {3} triangles drawn",
                              GeosetList(geosets), shown, hidden, visible));
        return visible;
    }

    /// <summary>
    /// Re-upload the textures of an already-built model and hand them to the materials it already
    /// has. The mesh, the GameObject and the materials themselves survive: a skin change in WMV
    /// swaps which image a material samples, nothing about the geometry.
    ///
    /// decodedTextures is the model's CURRENT texture set keyed by M2 slot -- the caller replaces
    /// the entries the new skin changed and leaves the rest alone, so slots the skin does not
    /// touch (an environment map named by the M2 itself, say) are simply re-uploaded unchanged.
    /// </summary>
    public static void RebindTextures(WmvRuntimeModel runtime, Dictionary<int, BlpImage> decodedTextures,
                                      string objectName, Action<string> log)
    {
        if (runtime == null || runtime.Materials.Length == 0)
            return;
        if (runtime.Bindings.Length != runtime.Materials.Length)
        {
            if (log != null)
                log("rebind: this model was built without texture bindings -- nothing to re-bind");
            return;
        }

        var fresh = new List<Texture2D>();
        var cache = new Dictionary<int, Texture2D>();

        for (int i = 0; i < runtime.Materials.Length; i++)
        {
            Material m = runtime.Materials[i];
            if (m == null)
                continue;
            WmvMaterialBinding b = runtime.Bindings[i];

            Texture2D tex = GetTexture(decodedTextures, cache, fresh, b.BaseSlot, b.DropAlpha, false, objectName);
            if (tex != null && !Debug_.MatColors)
                m.mainTexture = tex;

            Texture2D envTex = GetTexture(decodedTextures, cache, fresh, b.EnvSlot, true, true, objectName);
            if (m.HasProperty(CombinerModeProperty))
            {
                bool on = envTex != null && !Debug_.MatColors;
                if (on) m.SetTexture(SecondTexProperty, envTex);
                m.SetFloat(CombinerModeProperty, on ? PixelShaderOpaqueMod2xNaAlpha : 0);
            }

            if (log != null)
                log(string.Format("rebind: material '{0}' <- slot {1} (alpha {2}){3}",
                                  m.name, b.BaseSlot, b.DropAlpha ? "forced to 255" : "kept (combiner mask)",
                                  b.EnvSlot >= 0 ? ", env slot " + b.EnvSlot : ""));
        }

        // Only now destroy the old uploads: a material that ended up keeping its texture would
        // otherwise be left pointing at a destroyed one.
        foreach (var old in runtime.Textures)
            if (old != null && !fresh.Contains(old))
                UnityEngine.Object.Destroy(old);
        runtime.Textures = fresh.ToArray();
    }

    /// <summary>M2 pixel shader 12, "Combiners_Opaque_Mod2xNA_Alpha" -- the one combiner this
    /// milestone implements. See WmvOpaque.shader.</summary>
    const int PixelShaderOpaqueMod2xNaAlpha = 12;

    /// <summary>
    /// Resolve one TEXTURE UNIT of a batch to an M2 texture slot.
    ///
    /// A batch declares how many textures it loads (TextureCount) and where its run starts in the
    /// model's texture-combo table (TextureComboIndex); unit k is entry TextureComboIndex + k.
    /// Reading only the first entry -- which is what this used to do -- silently discards every
    /// unit past the first, and with it every multi-texture effect the model asks for.
    ///
    /// Unit 0 falls back to slot 0 when the lookup is malformed, because a model with no texture
    /// at all is worse than a model with the wrong one. A higher unit gets no such courtesy:
    /// guessing a texture for it would invent an effect the model never asked for.
    /// </summary>
    static int ResolveTextureSlot(M2ParsedModel model, M2Batch batch, int unit)
    {
        if (unit < 0 || unit >= batch.TextureCount)
            return -1;

        int comboIndex = batch.TextureComboIndex + unit;
        if (comboIndex >= 0 && comboIndex < model.TextureLookup.Length)
        {
            int slot = model.TextureLookup[comboIndex];
            if (slot >= 0 && slot < model.Textures.Length)
                return slot;
        }
        return (unit == 0 && model.Textures.Length > 0) ? 0 : -1;
    }

    /// <summary>
    /// Does the model want this batch drawn in its rest pose?
    ///
    /// A model hides geometry it is not currently using by keying an animation track to zero
    /// rather than by leaving the geometry out: an eye overlay, a glow, a blink. chicken2 is the
    /// worked example -- its 18-triangle eye batch points at a colour entry whose alpha track is
    /// 0, and the actual eye is painted into the skin texture on the head underneath. Drawing
    /// that batch anyway covers the painted eye with a flat red patch.
    ///
    /// This mirrors the legacy viewport's own gate (ModelRenderPass::init):
    ///
    ///     ocol.w &gt; 0  &amp;&amp;  (colorIndex == -1 || ecol.w &gt; 0)
    ///
    /// where ocol.w comes from the texture-weight track and ecol.w from the colour entry's alpha
    /// track -- and, note, ecol stays zero when the colour entry has no RGB track at all, so such
    /// a batch is hidden too. Only animation 0 at time 0 is considered: this milestone renders a
    /// static pose. When the model carries none of these tracks every batch is visible, which is
    /// the behaviour before any of this was read.
    /// </summary>
    static bool BatchIsVisible(M2ParsedModel model, M2Batch batch, out string reason)
    {
        reason = null;

        // texture weight (the transparency track)
        if (model.TextureWeights.Length > 0)
        {
            int weightIndex = -1;
            if (batch.TextureWeightComboIndex < model.TextureWeightLookup.Length)
                weightIndex = model.TextureWeightLookup[batch.TextureWeightComboIndex];
            else if (batch.TextureWeightComboIndex < model.TextureWeights.Length)
                weightIndex = batch.TextureWeightComboIndex;   // no lookup table: index directly

            if (weightIndex >= 0 && weightIndex < model.TextureWeights.Length &&
                model.TextureWeights[weightIndex] <= 0f)
            {
                reason = "texture weight " + weightIndex + " is 0";
                return false;
            }
        }

        // colour entry alpha
        if (batch.HasColor && model.Colors.Length > 0)
        {
            if (batch.ColorIndex >= model.Colors.Length)
                return true;                       // out of range: not our call to hide it
            M2ColorDef c = model.Colors[batch.ColorIndex];
            if (!c.HasColorTrack)
            {
                reason = "colour " + batch.ColorIndex + " has no track for animation 0";
                return false;
            }
            if (c.Alpha <= 0f)
            {
                reason = "colour " + batch.ColorIndex + " alpha is 0";
                return false;
            }
        }
        return true;
    }

    /// <summary>
    /// Upload (or reuse) one texture slot. Slots are shared between batches, so the cache is keyed
    /// by slot AND by how the alpha channel was treated -- the same slot can feed one batch whose
    /// alpha is the combiner mask and another where it is discarded.
    /// </summary>
    static Texture2D GetTexture(Dictionary<int, BlpImage> decodedTextures,
                                Dictionary<int, Texture2D> cache, List<Texture2D> owned,
                                int slot, bool dropAlpha, bool clamp, string objectName)
    {
        if (slot < 0 || decodedTextures == null)
            return null;
        BlpImage decoded;
        if (!decodedTextures.TryGetValue(slot, out decoded) || decoded == null)
            return null;

        int key = slot * 4 + (dropAlpha ? 2 : 0) + (clamp ? 1 : 0);
        Texture2D tex;
        if (cache.TryGetValue(key, out tex))
            return tex;

        tex = CreateTexture(decoded, objectName + "_tex" + slot + (dropAlpha ? "_opaque" : ""),
                            dropAlpha);
        if (clamp)
            tex.wrapMode = TextureWrapMode.Clamp;
        cache[key] = tex;
        owned.Add(tex);
        return tex;
    }

    /// <summary>
    /// Can the resolved shader run the M2 combiner? Only the renderer's own WmvOpaque shader can;
    /// a pipeline Lit shader has one texture slot and no idea what a sphere-mapped second unit is.
    /// Probed by property rather than by name so a future shader gets the same treatment for free.
    /// </summary>
    static bool ShaderHasCombiner()
    {
        if (combinerProbed)
            return combinerAvailable;
        combinerProbed = true;
        Shader s = FindRenderShader(null);
        if (s == null)
            return false;
        var probe = new Material(s);
        combinerAvailable = probe.HasProperty(CombinerModeProperty);
        UnityEngine.Object.Destroy(probe);
        return combinerAvailable;
    }

    const string CombinerModeProperty = "_CombinerMode";
    const string SecondTexProperty = "_SecondTex";
    static bool combinerProbed, combinerAvailable;

    static Texture2D CreateTexture(BlpImage img, string name, bool dropAlpha)
    {
        // ROW ORDER: a BLP stores its rows top-down (row 0 = top of the image), but a Unity
        // texture's raw data starts at the BOTTOM-left. Uploading the decoded bytes as-is
        // therefore lands the image upside down -- and since the UV converter already flips V
        // for Unity's bottom-up convention, the two flips cancel out and the model comes back
        // textured with the wrong part of the sheet. Flip the rows once, here, and the standard
        // Unity convention holds everywhere downstream.
        int stride = img.Width * 4;
        var pixels = new byte[img.Rgba.Length];
        for (int y = 0; y < img.Height; y++)
            Buffer.BlockCopy(img.Rgba, y * stride, pixels, (img.Height - 1 - y) * stride, stride);

        // ALPHA: on an opaque WoW material the alpha channel is not transparency, and on a real
        // creature skin it is nowhere near solid -- chicken2's is DXT5 with ~97% of its texels
        // below 255 (min 15, mean 240). WoW ignores it; anything on this side that lets it reach
        // a blend or an alpha test instead draws the bird as overlapping translucent shells. So
        // for an opaque material the channel is discarded here rather than trusted downstream.
        if (dropAlpha)
            for (int i = 3; i < pixels.Length; i += 4) pixels[i] = 255;

        // The decoder hands over mip 0 only. Upload it with SetPixelData(level 0) and let
        // Apply(updateMipmaps: true) build the rest -- LoadRawTextureData on a mip-chained
        // texture would expect data for every level and reject a mip-0-sized array.
        var tex = new Texture2D(img.Width, img.Height, TextureFormat.RGBA32, true) { name = name };
        tex.SetPixelData(pixels, 0);
        tex.Apply(true, false);
        tex.wrapMode = TextureWrapMode.Repeat;
        tex.filterMode = FilterMode.Bilinear;
        return tex;
    }

    /// <summary>
    /// Minimum viable material for this milestone. Two rules matter more than fidelity here:
    ///
    ///   * the WoW blend mode decides transparency, never the texture. A creature skin carries an
    ///     alpha channel whether or not the model wants blending, so keying off the texture turns
    ///     a solid bird see-through.
    ///   * the render state is set EXPLICITLY rather than inherited from the shader's defaults,
    ///     AND the shader is chosen so that setting it actually does something -- see
    ///     FindRenderShader. A shader that bakes its blending into the pass ignores every
    ///     property written here without complaining.
    ///
    /// This is deliberately not a full WoW material system; see the blend-mode note below.
    /// </summary>
    static Material CreateMaterial(M2MaterialDef def, M2BlendMode mode, Texture2D tex,
                                   Texture2D envTex, string name, Action<string> log)
    {
        var shader = FindRenderShader(log);
        // Material(null) throws; Unity's error shader is always present and at least draws
        // geometry, so a missing shader degrades to a visible (magenta) mesh, not a failed load.
        var m = new Material(shader != null ? shader : Shader.Find("Hidden/InternalErrorShader"))
        {
            name = name
        };
        if (tex != null && !Debug_.MatColors) m.mainTexture = tex;
        if (m.HasProperty("_Glossiness")) m.SetFloat("_Glossiness", 0f);      // Built-in
        else if (m.HasProperty("_Smoothness")) m.SetFloat("_Smoothness", 0f); // URP/HDRP

        // Second texture unit. The property only exists on the renderer's own shader, so on every
        // other shader this is a no-op and the material stays exactly as it was.
        if (m.HasProperty(CombinerModeProperty))
        {
            bool on = envTex != null && !Debug_.MatColors;
            if (on) m.SetTexture(SecondTexProperty, envTex);
            m.SetFloat(CombinerModeProperty, on ? PixelShaderOpaqueMod2xNaAlpha : 0);
        }

        string treatedAs;
        switch (mode)
        {
            case M2BlendMode.Opaque:
                SetOpaque(m);
                treatedAs = "opaque";
                break;

            // Alpha-key is a cutout in WoW. Plain Alpha is genuinely blended, but for a static
            // V1 creature a clip is the safer reading: it keeps depth writes and solid geometry
            // instead of risking a see-through model, and looks right for the hair/feather/eye
            // decals creatures actually use it for. Real blending is left for the material pass.
            case M2BlendMode.AlphaKey:
            case M2BlendMode.Alpha:
                treatedAs = SetAlphaClip(m, 0.5f) ? "alpha clip (cutoff 0.5)"
                                                  : "opaque (shader has no alpha-clip property)";
                if (mode == M2BlendMode.Alpha && log != null)
                    log("material: blend mode Alpha is drawn as an alpha clip in this milestone");
                break;

            default:
                SetOpaque(m);
                treatedAs = "opaque (blend mode not implemented)";
                if (log != null)
                    log(string.Format("material: blend mode {0} is not implemented yet -- drawing it opaque",
                                      def.BlendMode));
                break;
        }

        // Only the model's own flag may disable culling; nothing here turns it off globally.
        if (m.HasProperty("_Cull"))
            m.SetFloat("_Cull", (float)(def.TwoSided
                ? UnityEngine.Rendering.CullMode.Off
                : UnityEngine.Rendering.CullMode.Back));

        if (Debug_.MatColors)
        {
            var c = DebugColors[Mathf.Abs(name.GetHashCode()) % DebugColors.Length];
            if (m.HasProperty("_BaseColor")) m.SetColor("_BaseColor", c);
            if (m.HasProperty("_Color")) m.SetColor("_Color", c);
        }

        LogMaterialState(m, def, mode, treatedAs, log);
        return m;
    }

    /// <summary>Opaque, depth-writing, geometry queue -- stated explicitly across pipelines.</summary>
    static void SetOpaque(Material m)
    {
        if (m.HasProperty("_Mode")) m.SetFloat("_Mode", 0f);        // Built-in Standard
        if (m.HasProperty("_Surface")) m.SetFloat("_Surface", 0f);  // URP/HDRP: 0 = opaque
        if (m.HasProperty("_Blend")) m.SetFloat("_Blend", 0f);
        if (m.HasProperty("_AlphaClip")) m.SetFloat("_AlphaClip", 0f);
        if (m.HasProperty("_SrcBlend")) m.SetInt("_SrcBlend", (int)UnityEngine.Rendering.BlendMode.One);
        if (m.HasProperty("_DstBlend")) m.SetInt("_DstBlend", (int)UnityEngine.Rendering.BlendMode.Zero);
        if (m.HasProperty("_ZWrite")) m.SetInt("_ZWrite", 1);
        m.DisableKeyword("_ALPHATEST_ON");
        m.DisableKeyword("_ALPHABLEND_ON");
        m.DisableKeyword("_ALPHAPREMULTIPLY_ON");
        m.DisableKeyword("_SURFACE_TYPE_TRANSPARENT");
        m.SetOverrideTag("RenderType", "Opaque");
        m.renderQueue = (int)UnityEngine.Rendering.RenderQueue.Geometry;
    }

    /// <summary>
    /// Alpha-tested cutout: still opaque as far as depth and sorting are concerned. Returns false
    /// when the shader exposes no cutoff at all, in which case the material is left fully opaque
    /// -- for this milestone a solid surface beats a see-through one.
    /// </summary>
    static bool SetAlphaClip(Material m, float cutoff)
    {
        SetOpaque(m);                                                // depth/blend state first
        if (!m.HasProperty("_Cutoff") && !m.HasProperty("_AlphaCutoff"))
            return false;
        if (m.HasProperty("_Mode")) m.SetFloat("_Mode", 1f);         // Built-in Standard: cutout
        if (m.HasProperty("_AlphaClip")) m.SetFloat("_AlphaClip", 1f); // URP
        if (m.HasProperty("_Cutoff")) m.SetFloat("_Cutoff", cutoff);
        if (m.HasProperty("_AlphaCutoff")) m.SetFloat("_AlphaCutoff", cutoff); // HDRP
        m.EnableKeyword("_ALPHATEST_ON");
        m.SetOverrideTag("RenderType", "TransparentCutout");
        m.renderQueue = (int)UnityEngine.Rendering.RenderQueue.AlphaTest;
        return true;
    }

    /// <summary>
    /// Report what the material ACTUALLY is at runtime, not what it was asked to be. A property
    /// the shader does not expose reads "n/a", which is the interesting case: it could not be set,
    /// so whatever the shader bakes into its pass is what gets drawn.
    /// </summary>
    static void LogMaterialState(Material m, M2MaterialDef def, M2BlendMode mode,
                                 string treatedAs, Action<string> log)
    {
        if (log == null) return;
        Shader s = m.shader;
        log(string.Format(
            "material '{0}': shader '{1}' (shader default queue {2}) -> treated as {3}; " +
            "renderQueue {4} RenderType '{5}' _Mode {6} _Surface {7} _AlphaClip {8} _SrcBlend {9} " +
            "_DstBlend {10} _ZWrite {11} _Cull {12} _CombinerMode {13} keywords [{14}]; " +
            "WoW blend {15} depthWriteOff {16} twoSided {17}",
            m.name, s != null ? s.name : "<none>", s != null ? s.renderQueue : -1, treatedAs,
            m.renderQueue, m.GetTag("RenderType", false, "<unset>"),
            Prop(m, "_Mode"), Prop(m, "_Surface"), Prop(m, "_AlphaClip"), Prop(m, "_SrcBlend"),
            Prop(m, "_DstBlend"), Prop(m, "_ZWrite"), Prop(m, "_Cull"), Prop(m, CombinerModeProperty),
            string.Join(",", m.shaderKeywords), mode, def.DepthWriteDisabled, def.TwoSided));

        if (!shaderCanRenderOpaque)
            log("material '" + m.name + "': the resolved shader bakes blending, depth and culling " +
                "into its pass, so the state above is ADVISORY ONLY -- it cannot be applied and " +
                "the model will draw see-through. Add 'Standard' (or your pipeline's Lit shader) " +
                "to Project Settings > Graphics > Always Included Shaders and rebuild the player.");
    }

    /// <summary>A property's value, or "n/a" when the shader does not expose it.</summary>
    static string Prop(Material m, string name)
    {
        return m.HasProperty(name) ? m.GetFloat(name).ToString("0.##") : "n/a";
    }

    /// <summary>
    /// Name of the shader shipped in Assets/Resources/. A Resources folder is never stripped from
    /// a player build, which makes this the renderer's only guaranteed-present shader.
    /// </summary>
    const string OpaqueShaderResource = "WmvOpaque";

    // Resolved once per session; Shader.Find is not free and the answer cannot change.
    static Shader cachedShader;
    static bool shaderResolved;
    static bool shaderCanRenderOpaque;

    /// <summary>
    /// A shader to draw with, whatever pipeline the project uses and whatever survived build
    /// stripping.
    ///
    /// Two things make this harder in a PLAYER than in the editor: Shader.Find only sees shaders
    /// actually included in the build (a shader no asset references is stripped -- which is why a
    /// runtime-built material can come out magenta even though the project looks fine in the
    /// editor), and the name differs per render pipeline.
    ///
    /// The third thing, and the one that actually broke the render: a candidate is not usable just
    /// because Shader.Find returned it. Sprites/Default is always included, so it always "wins" a
    /// naive search -- and it bakes Blend One OneMinusSrcAlpha, ZWrite Off and Cull Off into its
    /// pass. Those are not material properties, so nothing this class writes can undo them, and an
    /// opaque WoW material silently renders as overlapping translucent shells. Every candidate is
    /// therefore screened by Accept() before it is taken, and the search starts from the active
    /// pipeline's own default material rather than from a name.
    ///
    /// Never throws: drawing the mesh with a fallback shader beats failing the load. If nothing
    /// opaque-capable exists, that is said plainly in the log rather than rendered quietly wrong.
    /// </summary>
    public static Shader ResolveShader(Action<string> log) { return FindRenderShader(log); }

    static Shader FindRenderShader(Action<string> log)
    {
        if (shaderResolved)
            return cachedShader;
        shaderResolved = true;

        // A shader written for one render pipeline draws MAGENTA under another, so the pipeline
        // decides which names may even be tried. null here means the built-in pipeline.
        bool builtIn = UnityEngine.Rendering.GraphicsSettings.currentRenderPipeline == null;

        // The pipeline's Lit shaders cannot run the M2 combiner, so -wmvOwnShader exists to make
        // the renderer's own shader win the search and show what the combiner does.
        if (Debug_.OwnShader &&
            Accept(Resources.Load<Shader>(OpaqueShaderResource),
                   "using the renderer's own '" + OpaqueShaderResource + "' shader (-wmvOwnShader)", log))
            return cachedShader;

        // Best case: a real lit shader for the pipeline in use. Shader.Find only sees what
        // survived build stripping, and a runtime-built material references nothing at build
        // time, so any of these can come back null in a player that looks fine in the editor.
        string[] lit = builtIn
            ? new[] { "Standard", "Legacy Shaders/Diffuse", "Mobile/Diffuse" }
            : new[] { "Universal Render Pipeline/Lit", "Universal Render Pipeline/Simple Lit",
                      "HDRP/Lit" };
        foreach (var name in lit)
            if (Accept(Shader.Find(name), "using shader '" + name + "'", log))
                return cachedShader;

        // Next: the material Unity gives a new primitive. Unity picks it from the ACTIVE pipeline,
        // so it is pipeline-correct by construction -- when it exists at all. In a stripped player
        // it can come back as the magenta error shader, which Accept() rejects.
        var probe = GameObject.CreatePrimitive(PrimitiveType.Quad);
        var probeRenderer = probe != null ? probe.GetComponent<MeshRenderer>() : null;
        var probeMaterial = probeRenderer != null ? probeRenderer.sharedMaterial : null;
        var probeShader = probeMaterial != null ? probeMaterial.shader : null;
        if (probe != null) UnityEngine.Object.Destroy(probe);
        if (Accept(probeShader, "no named lit shader survived build stripping -- using the active " +
                                "pipeline's default material shader '" +
                                (probeShader != null ? probeShader.name : "?") + "'", log))
            return cachedShader;

        // The guarantee. Assets/Resources/WmvOpaque.shader ships with the renderer and anything
        // under a Resources folder is always included in the build, so this rung cannot be
        // stripped away like the named lookups above. Unlit, but opaque, textured and with its
        // render state exposed as real properties -- which is what actually matters here.
        if (Accept(Resources.Load<Shader>(OpaqueShaderResource),
                   "no pipeline shader survived build stripping -- using the renderer's own " +
                   "'" + OpaqueShaderResource + "' shader. Lighting is a fixed viewer key light " +
                   "rather than the scene's; add a Lit shader to Project Settings > Graphics > " +
                   "Always Included Shaders and rebuild the player for full lighting.", log))
            return cachedShader;

        // Unlit but still opaque: flat lighting, correct solidity.
        string[] unlit = builtIn
            ? new[] { "Unlit/Texture" }
            : new[] { "Universal Render Pipeline/Unlit", "HDRP/Unlit" };
        foreach (var name in unlit)
            if (Accept(Shader.Find(name),
                       "falling back to the unlit shader '" + name + "'", log))
                return cachedShader;

        // Nothing opaque-capable left -- which should now mean Assets/Resources went missing.
        // Take a blended shader only so that something is visible at all, and say exactly what it
        // will look like and how to fix it.
        cachedShader = Shader.Find("Sprites/Default");
        shaderCanRenderOpaque = false;
        if (log != null)
            log(cachedShader != null
                ? "no opaque-capable shader found in this build -- falling back to 'Sprites/Default', " +
                  "which forces alpha blending, no depth write and no back-face culling. The model " +
                  "WILL look see-through. Copy Assets/Resources/ into the Unity project (it holds " +
                  "the renderer's own opaque shader) and rebuild the player."
                : "no shader could be resolved at all -- the model will draw with Unity's error " +
                  "shader (magenta). Copy Assets/Resources/ into the Unity project and rebuild " +
                  "the player.");
        return cachedShader;
    }

    /// <summary>
    /// Take a candidate only if an opaque WoW material can actually be drawn solid with it.
    /// A shader either exposes the blend/depth knobs (_ZWrite, _SrcBlend, _Surface, _Mode) so the
    /// state can be set, or it is opaque already -- which its own default render queue reports:
    /// opaque shaders sort in Geometry, baked-transparent ones in Transparent. A shader that is
    /// neither controllable nor opaque (Sprites/Default, the UI and particle shaders) would
    /// swallow every state call in silence, so it is skipped and the reason logged.
    /// </summary>
    static bool Accept(Shader s, string message, Action<string> log)
    {
        if (s == null)
            return false;

        // The error shader draws magenta by definition, and it sorts in the geometry queue, so
        // the opacity test below would happily wave it through. It is never an answer.
        if (s.name != null && s.name.StartsWith("Hidden/"))
        {
            if (log != null)
                log("skipping shader '" + s.name + "': it is one of Unity's internal shaders " +
                    "(the magenta error shader is what a stripped build hands back here)");
            return false;
        }

        var probe = new Material(s);
        bool controllable = probe.HasProperty("_ZWrite") || probe.HasProperty("_SrcBlend") ||
                            probe.HasProperty("_Surface") || probe.HasProperty("_Mode");
        UnityEngine.Object.Destroy(probe);

        bool naturallyOpaque = s.renderQueue < (int)UnityEngine.Rendering.RenderQueue.AlphaTest;
        if (!controllable && !naturallyOpaque)
        {
            if (log != null)
                log("skipping shader '" + s.name + "': it bakes transparency into its pass " +
                    "(default queue " + s.renderQueue + ", no blend or depth properties), so an " +
                    "opaque WoW material could not be drawn solid with it");
            return false;
        }

        cachedShader = s;
        shaderCanRenderOpaque = true;
        if (log != null) log(message);
        return true;
    }
}
