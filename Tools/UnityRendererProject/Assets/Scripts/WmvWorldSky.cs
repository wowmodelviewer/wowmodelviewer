// WmvWorldSky.cs
//
// THE SKY (Stage 10 step 4, docs/world-viewer/10-light-sky-fog.md 6.8, 6.12): the colour dome (Wow/World/SkyDome.cs) drawn
// behind the world for the main camera's world view, and URP's bloom held at 0 while the outdoor light is on there. Driven by
// WmvWorldLight (Sky, from the main camera's beginCameraRendering); nothing here runs, is made or is changed with
// WMV_LIGHT=off, with the light off, or outside the world view.
//   the pass      one ScriptableRenderPass at AfterRenderingSkybox -- after the opaque world, before the water's grab
//                 (WmvWaterGrabPass, BeforeRenderingTransparents, so its copies hold the sky; the self-test asserts the
//                 order) --, enqueued for the main camera's perspective world view only (never the top-down overview, never
//                 another camera), while the light is on and valid with its sky colours and -wmvSky is not off. It draws the
//                 dome (Resources/WmvSky.shader) into the colour attachment with the depth attachment READ ONLY: the shader
//                 puts the dome on the far plane and tests against the cleared far depth without writing it, so the sky fills
//                 exactly the pixels nothing opaque covers and the water grab's coverage (0 at the far depth) is unchanged.
//   the inputs    the vertex colours, the tint and the sun of SkyInputs as the material's own values (_WmvSkyColour0..5,
//                 _WmvSkyTint, _WmvSkyWeights, _WmvSkySun), set before each enqueue: authored values as Vector4 (byte / 255,
//                 never a Unity Color, which would be linearised).
//   bloom         while the light is on in the world view every volume's Bloom intensity is overridden to 0 (URP's bloom is
//                 not client data, plan D9); on leaving the world view, switching the light off or Release each is put back
//                 to its own override state and value, exactly. WmvMain.ConfigureDisplayTransform edits the same profile
//                 instances (Volume.profile) at start for Tonemapping and Vignette and never Bloom, so the two never write
//                 the same parameter.
//   resources     the dome mesh and its material are made at the first draw (none exists while no sky is drawn: Stage 09's
//                 mesh and material tallies stay) and destroyed with the scene and when the light goes off
//                 (WmvWorldLight.Off: clearTerrain, a model load, a new scene, setWorldLight off, a fault) and by Release
//                 (WmvWorldLight's owner's), so runtimeState's liveMeshes and liveMaterials are Stage 09's again after a clear.

using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.RenderGraphModule;
using UnityEngine.Rendering.Universal;
using Wmv.Wow.World;

public static class WmvWorldSky
{
    public const string ShaderResource = "WmvSky";
    /// <summary>The sky pass's event: after the opaque world, before the water's grab (WmvWaterGrabPass,
    /// BeforeRenderingTransparents).</summary>
    public const RenderPassEvent Event = RenderPassEvent.AfterRenderingSkybox;
    public static readonly int[] ColourIds =
    {
        Shader.PropertyToID("_WmvSkyColour0"), Shader.PropertyToID("_WmvSkyColour1"), Shader.PropertyToID("_WmvSkyColour2"),
        Shader.PropertyToID("_WmvSkyColour3"), Shader.PropertyToID("_WmvSkyColour4"), Shader.PropertyToID("_WmvSkyColour5"),
    };
    public static readonly int TintId = Shader.PropertyToID("_WmvSkyTint"), WeightsId = Shader.PropertyToID("_WmvSkyWeights"),
                               SunId = Shader.PropertyToID("_WmvSkySun"), LinearOutId = Shader.PropertyToID("_WmvLinearOut");

    static Mesh mesh;
    static bool meshTableB;
    static Material material;
    static bool shaderMissingSaid;
    static SkyPass pass;

    /// <summary>The self-test's: the passes recorded, the frame of the last, and WmvWaterGrabPass.Runs when it was recorded
    /// (the grab records after the sky in the same frame exactly when its Runs is then one more).</summary>
    public static int Records, LastRecordFrame = -1, GrabRunsAtRecord = -1;

    /// <summary>The dome mesh and material alive (0 or 1 each): the self-test's tallies leave them out.</summary>
    public static int MaterialCount { get { return material != null ? 1 : 0; } }
    public static int MeshCount { get { return mesh != null ? 1 : 0; } }

    /// <summary>The dome's arrays as built (the CPU twin's), table A or B.</summary>
    public static void DomeArrays(bool tableB, out float[] positions, out float[] slots, out int[] indices)
    {
        positions = new float[3 * SkyDome.VertexCount];
        slots = new float[SkyDome.VertexCount];
        indices = new int[3 * SkyDome.TriangleCount];
        SkyDome.Build(tableB, positions, slots, indices);
    }

    /// <summary>The dome as a Unity mesh: positions in the table's units (y up), the colour slot in UV0.x.</summary>
    public static Mesh BuildMesh(bool tableB)
    {
        float[] p, s;
        int[] ix;
        DomeArrays(tableB, out p, out s, out ix);
        var v = new Vector3[SkyDome.VertexCount];
        var uv = new Vector2[SkyDome.VertexCount];
        for (int k = 0; k < v.Length; k++)
        {
            v[k] = new Vector3(p[3 * k], p[3 * k + 1], p[3 * k + 2]);
            uv[k] = new Vector2(s[k], 0f);
        }
        var m = new Mesh { name = "WmvSky dome (table " + (tableB ? "b" : "a") + ")", hideFlags = HideFlags.HideAndDontSave };
        m.indexFormat = IndexFormat.UInt16;
        m.SetVertices(v);
        m.SetUVs(0, uv);
        m.SetTriangles(ix, 0, false);
        // Far beyond any camera: the dome is drawn at the camera, never culled by its bounds.
        m.bounds = new Bounds(Vector3.zero, new Vector3(1e9f, 1e9f, 1e9f));
        m.UploadMeshData(true);
        return m;
    }

    /// <summary>A material of Resources/WmvSky.shader (the asset by name, then the registry), null when the build lacks it.</summary>
    public static Material CreateMaterial()
    {
        Shader s = Resources.Load<Shader>(ShaderResource);
        if (s == null)
            s = Shader.Find("Wmv/Sky");
        if (s == null || !s.isSupported)
            return null;
        var m = new Material(s) { name = "WmvSky", hideFlags = HideFlags.HideAndDontSave };
        m.SetFloat(LinearOutId, WmvModelBuilder.AuthoredTextureDomain ? 0f : 1f);
        return m;
    }

    /// <summary>SkyInputs as the material's values (authored, Vector4).</summary>
    public static void SetInputs(Material m, SkyInputs s)
    {
        for (int k = 0; k < SkyDome.ColourSlots; k++)
            m.SetVector(ColourIds[k], new Vector4((float)s.Colours[3 * k], (float)s.Colours[3 * k + 1], (float)s.Colours[3 * k + 2], 1f));
        m.SetVector(TintId, new Vector4((float)s.Tint[0], (float)s.Tint[1], (float)s.Tint[2], (float)s.Angle));
        m.SetVector(WeightsId, new Vector4((float)s.WeightAbove, (float)s.WeightBelow, 0f, 0f));
        m.SetVector(SunId, new Vector4((float)s.Sun[0], (float)s.Sun[1], (float)s.Sun[2], 0f));
    }

    /// <summary>Does `camera` get the sky: the main game camera of the world view, perspective, not a depth-only target?</summary>
    public static bool Wants(Camera camera, bool worldView)
    {
        if (!worldView || camera == null || camera != Camera.main || camera.cameraType != CameraType.Game || camera.orthographic)
            return false;
        if ((camera.cullingMask & WmvTerrainBuilder.TerrainViewMask) == 0)
            return false;
        return camera.targetTexture == null || camera.targetTexture.format != RenderTextureFormat.Depth;
    }

    /// <summary>The sky for this render of `camera` (WmvWorldLight.Sky): the inputs set and the pass enqueued. False when
    /// nothing is drawn (the camera is not the world view's, the shader is missing, no renderer).</summary>
    public static bool Enqueue(Camera camera, bool worldView, SkyInputs s, bool tableB, Action<string> log)
    {
        if (!Wants(camera, worldView) || s == null)
            return false;
        if (material == null)
        {
            material = CreateMaterial();
            if (material == null)
            {
                if (!shaderMissingSaid && log != null)
                    log("sky: Resources/" + ShaderResource + ".shader is missing from this player build -- no sky");
                shaderMissingSaid = true;
                return false;
            }
        }
        if (mesh == null || meshTableB != tableB)
        {
            if (mesh != null)
                UnityEngine.Object.DestroyImmediate(mesh);
            mesh = BuildMesh(tableB);
            meshTableB = tableB;
        }
        UniversalAdditionalCameraData data = camera.GetUniversalAdditionalCameraData();
        if (data == null || data.scriptableRenderer == null)
            return false;
        SetInputs(material, s);
        if (pass == null)
            pass = new SkyPass();
        data.scriptableRenderer.EnqueuePass(pass);
        return true;
    }

    /// <summary>The dome mesh and material destroyed (WmvWorldLight.Off and Release; the self-test after its fixtures); the next
    /// draw makes them again.</summary>
    public static void DropResources()
    {
        if (mesh != null)
            UnityEngine.Object.DestroyImmediate(mesh);
        if (material != null)
            UnityEngine.Object.DestroyImmediate(material);
        mesh = null;
        material = null;
    }

    sealed class SkyPass : ScriptableRenderPass
    {
        public SkyPass()
        {
            renderPassEvent = Event;
            profilingSampler = new ProfilingSampler("WmvSky");
        }

        sealed class PassData
        {
            public Mesh Mesh;
            public Material Material;
        }

        public override void RecordRenderGraph(RenderGraph renderGraph, ContextContainer frameData)
        {
            if (mesh == null || material == null)
                return;
            UniversalResourceData resources = frameData.Get<UniversalResourceData>();
            TextureHandle colour = resources.activeColorTexture, depth = resources.activeDepthTexture;
            if (!colour.IsValid() || !depth.IsValid())
                return;
            Records++;
            LastRecordFrame = Time.frameCount;
            GrabRunsAtRecord = WmvWaterGrabPass.Runs;
            using (IRasterRenderGraphBuilder b = renderGraph.AddRasterRenderPass("WmvSky: the colour dome", out PassData d))
            {
                d.Mesh = mesh;
                d.Material = material;
                b.SetRenderAttachment(colour, 0, AccessFlags.ReadWrite);
                b.SetRenderAttachmentDepth(depth, AccessFlags.Read);
                b.AllowPassCulling(false);
                b.SetRenderFunc(static (PassData data, RasterGraphContext context) =>
                {
                    context.cmd.DrawMesh(data.Mesh, Matrix4x4.identity, data.Material, 0, 0);
                });
            }
        }
    }

    // ---------------------------------------------------------------- bloom (plan D9)

    sealed class HeldBloom
    {
        public Bloom Bloom;
        public bool Override;
        public float Value;
    }

    static readonly List<HeldBloom> held = new List<HeldBloom>();
    static bool holding;

    /// <summary>Bloom is held at 0 now.</summary>
    public static bool BloomHeld { get { return holding; } }

    /// <summary>Hold every volume's Bloom intensity at 0 (true), or put each back exactly as it was (false). Idempotent; only
    /// a change writes anything.</summary>
    public static void HoldBloom(bool hold, Action<string> log)
    {
        if (hold == holding)
            return;
        if (hold)
        {
            held.Clear();
            Volume[] volumes = UnityEngine.Object.FindObjectsByType<Volume>(FindObjectsInactive.Exclude);
            foreach (Volume v in volumes)
            {
                if (v == null || v.profile == null)
                    continue;
                Bloom b;
                if (!v.profile.TryGet(out b) || b == null)
                    continue;
                held.Add(new HeldBloom { Bloom = b, Override = b.intensity.overrideState, Value = b.intensity.value });
                b.intensity.overrideState = true;
                b.intensity.value = 0f;
            }
            holding = true;
            if (log != null)
                log("bloom: held at 0 while the outdoor light is on in the world view (" + held.Count + " volume(s) with Bloom; plan D9)");
            return;
        }
        int restored = 0;
        foreach (HeldBloom h in held)
            if (h.Bloom != null)
            {
                h.Bloom.intensity.value = h.Value;
                h.Bloom.intensity.overrideState = h.Override;
                restored++;
            }
        held.Clear();
        holding = false;
        if (log != null)
            log("bloom: put back as it was (" + restored + " volume(s))");
    }
}
