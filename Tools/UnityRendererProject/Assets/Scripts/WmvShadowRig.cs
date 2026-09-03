// WmvShadowRig.cs
//
// CAST SHADOWS for the preview light rig: the model occluding its own key light. A saddle rope
// falling across a mount's body, a horn across a face -- effects the rig's normal-based terms
// cannot produce, because they only know which way a surface faces, not what stands between it
// and the light.
//
// The machinery is one orthographic camera parked at the key light's position, rendering the
// model's depth into a texture every frame. WmvOpaque.shader projects each fragment into that
// map and compares depths: something nearer to the light than the fragment means the key is
// blocked there (see WmvShadowFactor). The pipeline's own shadow system is deliberately not
// used -- this renderer's shader has no LightMode tags and carries its own lighting, so it
// would never receive engine shadow data; a map it renders itself works identically under the
// built-in pipeline and URP, which is the same portability bargain the rest of the shader makes.
//
// Two properties fall out of the design and are worth naming:
//
//   * THE SHADOW FOLLOWS THE KEY, AND THE KEY IS ANCHORED TO THE WORLD VERTICAL. The rig
//     blends the camera-relative key direction toward world-up (WorldAnchor) and publishes the
//     result as _WmvKeyDirWorld each frame -- the one vector that feeds the shading, this
//     shadow camera and the contact march, so none of them can disagree. The anchor is what
//     the reference viewers measurably do (frame analysis of preview footage):
//     orbiting never flips a model's lit side there, and a camera under the model finds the
//     belly still dark, which a fully camera-relative key gets wrong.
//
//   * THE DEPTH PASS IS THE ORDINARY RENDER. The shadow camera draws the model with its normal
//     materials and simply keeps the depth buffer. That means alpha-keyed batches (hair cards,
//     fur flaps) clip in the shadow pass exactly as they clip on screen, so they cast shaped
//     shadows rather than solid slabs -- and blended or additive batches, which write no depth,
//     cast nothing, which is right for glows.
//
// Everything is gated by the _WmvShadowValid global, which unset reads 0: a player where this
// component never ran renders exactly as before.

using UnityEngine;

public class WmvShadowRig : MonoBehaviour
{
    /// <summary>
    /// The key light's direction in VIEW space, before the world anchor: x right, y up, z
    /// toward the viewer. MUST match KEY_DIR in WmvOpaque.shader -- the shader falls back to
    /// its copy in a player where this rig never ran, and if the two drift the fallback stops
    /// matching the real thing. With WorldAnchor at 1.0 this vector only decides that fallback:
    /// the anchored direction is world-up regardless of it.
    /// </summary>
    public static readonly Vector3 KeyDirView = new Vector3(0.081f, 0.858f, 0.507f);

    /// <summary>The sky fill, same handling. MUST match FILL_DIR in WmvOpaque.shader.</summary>
    public static readonly Vector3 FillDirView = new Vector3(0.059f, 0.998f, 0.032f);

    /// <summary>
    /// How much of the light direction is pinned to the world's vertical rather than the
    /// camera: 0 is the old fully camera-relative behaviour, 1 is a light pointing straight
    /// down from the world's sky regardless of the camera. Shipped at 1.0: the key is the
    /// world's vertical, full stop, so neither orbiting nor pitching moves a model's lit side,
    /// and the view-space tilt in KeyDirView is inert except in the shader's fallback. (Settled
    /// by eye on a live tuning panel that has since been removed; 0.7 was the earlier value.)
    /// </summary>
    public static readonly float WorldAnchor = 1.0f;

    // 4096 over a bounds-tight orthographic window puts a texel around 2.5 mm on a mount and
    // under 1 mm on a humanoid. Resolution is not cosmetic here: the depth and normal biases
    // scale with the texel, so doubling the map HALVES the distance below which an occluder
    // casts nothing -- the difference between a hood shadow that reaches the brow line and one
    // that stops a centimetre short of it.
    const int MapSize = 4096;
    // The view-depth buffer for the contact march. Screen-ish resolution is enough: the march
    // asks "is a surface in front of this ray", not "where exactly is its edge".
    const int ViewDepthSize = 2048;
    // The window is fitted to the model bounds at load; animation moves limbs outside the rest
    // pose, so give it margin rather than chase the pose every frame.
    const float Padding = 1.4f;

    Camera shadowCam;
    RenderTexture map;
    Camera depthCam;
    RenderTexture viewDepth;
    Bounds bounds;
    bool hasBounds;

    /// <summary>A model was (re)built: fit the shadow window around it.</summary>
    public void SetBounds(Bounds b)
    {
        bounds = b;
        hasBounds = true;
    }

    void EnsureResources()
    {
        if (shadowCam == null)
        {
            var go = new GameObject("WmvShadowCamera");
            go.transform.SetParent(transform, false);
            shadowCam = go.AddComponent<Camera>();
            shadowCam.enabled = false;               // rendered by hand, below
            shadowCam.orthographic = true;
            shadowCam.clearFlags = CameraClearFlags.SolidColor;
            shadowCam.backgroundColor = Color.black; // irrelevant: only depth is kept
            shadowCam.allowHDR = false;
            shadowCam.allowMSAA = false;
            shadowCam.aspect = 1f;
        }
        if (map == null)
        {
            // A depth-format target IS the shadow map: the colour result is discarded and the
            // depth buffer is sampled directly (sampler2D_float in the shader).
            map = new RenderTexture(MapSize, MapSize, 24, RenderTextureFormat.Depth)
            {
                name = "WmvShadowMap",
                filterMode = FilterMode.Point,
                wrapMode = TextureWrapMode.Clamp,
            };
        }
        if (depthCam == null)
        {
            // The contact march's eyes: the same scene from the VIEWER's pose, depth only. A
            // second camera rather than the pipeline's depth texture because the pipeline is
            // not guaranteed to make one (URP's is a project-asset setting this repo does not
            // control), and this renderer already lives by rendering its own.
            var go = new GameObject("WmvViewDepthCamera");
            go.transform.SetParent(transform, false);
            depthCam = go.AddComponent<Camera>();
            depthCam.enabled = false;
            depthCam.clearFlags = CameraClearFlags.SolidColor;
            depthCam.backgroundColor = Color.black;
            depthCam.allowHDR = false;
            depthCam.allowMSAA = false;
        }
        if (viewDepth == null)
        {
            viewDepth = new RenderTexture(ViewDepthSize, ViewDepthSize, 24,
                                          RenderTextureFormat.Depth)
            {
                name = "WmvViewDepth",
                filterMode = FilterMode.Point,
                wrapMode = TextureWrapMode.Clamp,
            };
        }
    }

    void LateUpdate()
    {
        // After every Update (the orbit camera moves in Update), before rendering: the map is
        // always in step with this frame's camera.
        Camera view = Camera.main;
        if (view != null)
            RenderFor(view);
    }

    /// <summary>
    /// Render the shadow map for the key light as seen from this camera, and publish the
    /// globals the shader samples with. Public so the light check can render the map for ITS
    /// camera -- a deterministic pose -- instead of measuring under whatever orientation the
    /// viewport happened to have.
    /// </summary>
    public void RenderFor(Camera view)
    {
        if (!hasBounds)
        {
            Shader.SetGlobalFloat("_WmvShadowValid", 0f);
            Shader.SetGlobalFloat("_WmvContactValid", 0f);
            return;
        }
        EnsureResources();

        // The key direction: view space -> world space through the viewer camera's rotation,
        // then blended toward the world's own up. The anchor is why preview lighting holds
        // still while the model is orbited, and why looking up from below does not drag the
        // light under the model.
        float anchor = WorldAnchor;
        Vector3 dirWorld = AnchoredDir(view, KeyDirView, anchor);
        Vector3 fillWorld = AnchoredDir(view, FillDirView, anchor);

        // A tight orthographic window around the model, looking back down the light direction.
        // Tight matters twice: texels cover the model rather than empty space, and the depth
        // range stays short, which is what keeps a fixed depth bias small.
        float r = Mathf.Max(bounds.extents.magnitude, 0.01f) * Padding;
        float dist = 2f * r;
        shadowCam.transform.position = bounds.center + dirWorld * dist;
        shadowCam.transform.rotation = Quaternion.LookRotation(-dirWorld);
        shadowCam.orthographicSize = r;
        shadowCam.nearClipPlane = dist - r;
        shadowCam.farClipPlane = dist + r;
        shadowCam.cullingMask = view.cullingMask;

        // The depth pass draws the model with its NORMAL materials, whose shader samples
        // _WmvShadowMap -- the very texture this render writes. Reading and writing one
        // resource in the same pass is a hazard D3D11 resolves by silently unbinding the read,
        // which happens to give the right answer but spams runtime warnings and is undefined
        // by contract. So the shadow path is switched off for the duration: the shader
        // early-outs on _WmvShadowValid and samples nothing.
        Shader.SetGlobalFloat("_WmvShadowValid", 0f);
        Shader.SetGlobalTexture("_WmvShadowMap", Texture2D.whiteTexture);
        shadowCam.targetTexture = map;
        shadowCam.Render();
        shadowCam.targetTexture = null;

        // World -> the light's clip space. renderIntoTexture must be FALSE here, and the
        // reason is subtle enough to have shipped wrong once: on D3D the camera rasterises
        // into the texture with a y-FLIPPED projection (rows land top-down), and a texture
        // sample's v=0 also addresses the top row -- the two flips cancel. Building the
        // sampling matrix WITH the flip (true) re-introduces it, and every lookup lands on the
        // vertically mirrored texel: the whole model's silhouette stamped upside-down across
        // itself, shadows on the top of the back where a high key can never put them. The z
        // row is identical either way, so the reversed-Z depth comparison is unaffected.
        Matrix4x4 gpuProj = GL.GetGPUProjectionMatrix(shadowCam.projectionMatrix, false);
        Shader.SetGlobalMatrix("_WmvShadowMatrix", gpuProj * shadowCam.worldToCameraMatrix);
        Shader.SetGlobalTexture("_WmvShadowMap", map);
        Shader.SetGlobalFloat("_WmvShadowTexel", 1f / MapSize);

        // Both biases derive from the map's footprint instead of being tuned by hand: the
        // normal offset is one texel of world size (enough that a surface never samples its
        // own depth), and the depth bias is one texel of the [0,1] depth range. The bias IS
        // the map's contact blind zone, so it is kept as small as stability allows and the
        // screen-space march below covers what remains.
        float texelWorld = 2f * r / MapSize;
        Shader.SetGlobalFloat("_WmvShadowNormalBias", 1.0f * texelWorld);
        Shader.SetGlobalFloat("_WmvShadowDepthBias", 1.0f / MapSize);
        Shader.SetGlobalFloat("_WmvShadowValid", 1f);

        // ---- the view-depth buffer, for the contact march --------------------------------
        //
        // The viewer camera's pose, but near/far PINCHED around the model: hardware depth
        // spends its precision near the near plane, and a preview camera's own far plane is
        // wildly generous. With the range tight the [0,1] depth units the march compares in
        // correspond to a roughly constant world thickness across the model.
        Vector3 toCenter = bounds.center - view.transform.position;
        float viewDist = toCenter.magnitude;
        float modelR = Mathf.Max(bounds.extents.magnitude, 0.01f) * Padding;
        depthCam.transform.position = view.transform.position;
        depthCam.transform.rotation = view.transform.rotation;
        depthCam.orthographic = false;
        depthCam.fieldOfView = view.fieldOfView;
        depthCam.aspect = view.aspect;
        depthCam.nearClipPlane = Mathf.Max(0.01f, viewDist - modelR);
        depthCam.farClipPlane = viewDist + modelR;
        depthCam.cullingMask = view.cullingMask;

        Shader.SetGlobalFloat("_WmvContactValid", 0f);   // same read-write hazard as the map
        Shader.SetGlobalTexture("_WmvViewDepth", Texture2D.whiteTexture);
        depthCam.targetTexture = viewDepth;
        depthCam.Render();
        depthCam.targetTexture = null;

        Matrix4x4 viewProj = GL.GetGPUProjectionMatrix(depthCam.projectionMatrix, false)
                             * depthCam.worldToCameraMatrix;
        Shader.SetGlobalMatrix("_WmvViewDepthMatrix", viewProj);
        Shader.SetGlobalTexture("_WmvViewDepth", viewDepth);
        Shader.SetGlobalVector("_WmvKeyDirWorld",
                               new Vector4(dirWorld.x, dirWorld.y, dirWorld.z, 0f));
        Shader.SetGlobalVector("_WmvFillDirWorld",
                               new Vector4(fillWorld.x, fillWorld.y, fillWorld.z, 0f));
        Shader.SetGlobalFloat("_WmvModelRadius", modelR);
        // Self-hit guard and thickness, in the pinched camera's [0,1] depth units (the range
        // spans ~2 model radii of world, so world-relative values divide by that).
        //
        // THICKNESS IS THE KNIFE-EDGE OF THIS TECHNIQUE. A first version assumed occluders
        // 0.20 R thick, and the result was a faint even wash over every large surface: any
        // geometry anywhere within a fifth of the model IN FRONT of the ray -- the far side of
        // a fold, the silhouette of the cloak -- counted as touching. Contact shadows are about
        // the near field, so the assumed thickness matches the march range itself: an occluder
        // matters only if the ray passes within touching distance BEHIND it. The guard against
        // a fragment finding its own surface is ~1 % of the model, paired with the ray's
        // starting push off the surface in the shader.
        float depthRange = 2f * modelR;
        Shader.SetGlobalFloat("_WmvContactEps", 0.010f * modelR / depthRange);
        Shader.SetGlobalFloat("_WmvContactThick", 0.08f * modelR / depthRange);
        Shader.SetGlobalFloat("_WmvContactValid", 1f);
    }

    /// <summary>
    /// A view-space light direction, made world: rotate it out through the camera, then pull
    /// it toward world-up by the anchor. The guard covers the one degenerate pose -- a camera
    /// pitched so far that the rotated direction opposes up and the blend cancels out.
    /// </summary>
    static Vector3 AnchoredDir(Camera view, Vector3 dirView, float anchor)
    {
        Vector3 w = view.transform.rotation * dirView;
        Vector3 blended = w * (1f - anchor) + Vector3.up * anchor;
        if (blended.sqrMagnitude < 1e-6f)
            return Vector3.up;
        return blended.normalized;
    }

    void OnDisable()
    {
        Shader.SetGlobalFloat("_WmvShadowValid", 0f);
        Shader.SetGlobalFloat("_WmvContactValid", 0f);
        Shader.SetGlobalVector("_WmvKeyDirWorld", new Vector4(0f, 0f, 0f, 0f));
        Shader.SetGlobalVector("_WmvFillDirWorld", new Vector4(0f, 0f, 0f, 0f));
        if (map != null)
        {
            map.Release();
            Destroy(map);
            map = null;
        }
        if (viewDepth != null)
        {
            viewDepth.Release();
            Destroy(viewDepth);
            viewDepth = null;
        }
    }
}
