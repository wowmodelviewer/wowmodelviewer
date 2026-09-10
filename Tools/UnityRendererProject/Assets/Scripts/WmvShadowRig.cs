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
    /// The contact shadow's seven controls, and the only place their defaults are written.
    ///
    /// They are here rather than in the shader because the application exposes them as sliders:
    /// a look is found by moving them and watching, not by rebuilding a shader. RenderFor
    /// publishes all seven every frame, so a player launched with no host -- the headless
    /// self-tests, or a developer running the exe on its own -- renders from exactly these
    /// numbers, which are the ones that used to be #defines in WmvOpaque.shader.
    ///
    /// Reach, Thickness and Bias are all fractions of the MODEL RADIUS, never absolute
    /// distances: that is what keeps a setting meaning the same thing on a 0.15-radius shoulder
    /// pad and a 12-radius boss. Softness is the tangent of the occlusion cone's half-angle,
    /// which is a ratio and so needs no scaling at all. Steps and Taps are sampling rates, not
    /// look controls -- they decide how finely the march resolves the shape the other five
    /// describe, and raising them cannot change what the acceptance test accepts.
    /// </summary>
    public static float ContactStrength = 0.4f;      // of the light one contact removes
    public static float ContactReach = 0.36666667f;  // how far the probe looks, x model radius
    public static float ContactSoftness = 0.25f;     // cone half-angle, as a tangent
    public static float ContactThickness = 0.08f;    // assumed occluder thickness, x model radius
    public static float ContactBias = 0.010f;        // self-hit guard, x model radius
    public static int ContactSteps = 32;             // samples along the reach
    public static int ContactTaps = 8;               // samples across the cone

    /// <summary>
    /// The shipped defaults, so a caller can put the controls back without knowing the numbers.
    /// </summary>
    public static void ResetContactSettings()
    {
        ContactStrength = 0.4f;
        ContactReach = 0.36666667f;
        ContactSoftness = 0.25f;
        ContactThickness = 0.08f;
        ContactBias = 0.010f;
        ContactSteps = 32;
        ContactTaps = 8;
    }

    /// <summary>
    /// Take a set of controls from the host. Clamped here rather than trusted: the values arrive
    /// over a socket, and a reach of zero or a tap count of zero would divide by nothing in the
    /// shader. The ceilings are generous -- they exist to stop a typo costing a frame, not to
    /// express an opinion about what looks right.
    /// </summary>
    public static void SetContactSettings(float strength, float reach, float softness,
                                          float thickness, float bias, int steps, int taps)
    {
        ContactStrength = Mathf.Clamp01(strength);
        ContactReach = Mathf.Clamp(reach, 0.001f, 2f);
        ContactSoftness = Mathf.Clamp(softness, 0f, 2f);
        ContactThickness = Mathf.Clamp(thickness, 0.001f, 1f);
        ContactBias = Mathf.Clamp(bias, 0f, 0.5f);
        ContactSteps = Mathf.Clamp(steps, 1, 128);
        ContactTaps = Mathf.Clamp(taps, 1, 32);
    }

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
        // wildly generous.
        //
        // What used to stand here was "with the range tight the [0,1] depth units the march
        // compares in correspond to a roughly constant world thickness across the model". That
        // was false, and it was the whole defect: perspective depth is hyperbolic, so the same
        // physical gap maps to a depth difference that varies as ((D+z)/(D-z))^2 with the
        // distance z from the camera -- at the shipped framing distance 5.4x between the near
        // and the far side of the geometry, 12.6x across the whole frustum -- and collapses
        // towards zero as the camera closes in. The march no longer compares device depths at all; it inverts them
        // (WmvLinearViewDepth) and compares distances. These planes now exist only to keep the
        // buffer's precision on the model, and to be the constants that inversion needs.
        //
        // AXIAL depth, not radial distance: near and far are PLANES, so the quantity that has
        // to bracket the model is its extent along the camera's forward axis. Radial distance
        // is always >= axial depth, so using it can only push the near plane INTO the model.
        // Un-panned the two agree, because the orbit camera sits on a ray through the bounds
        // centre; WmvOrbitCamera pans the PIVOT while the bounds centre stays put, and at
        // thirty degrees off-axis and seven radii out the radial form puts the near plane a
        // whole radius inside the geometry and clips the model's front half out of the buffer,
        // which reads as contact shadows missing over exactly that region.
        Vector3 toCenter = bounds.center - view.transform.position;
        float viewDist = Vector3.Dot(toCenter, view.transform.forward);
        float modelR = Mathf.Max(bounds.extents.magnitude, 0.01f) * Padding;
        depthCam.transform.position = view.transform.position;
        depthCam.transform.rotation = view.transform.rotation;
        depthCam.orthographic = false;
        depthCam.fieldOfView = view.fieldOfView;
        depthCam.aspect = view.aspect;
        // NEAR-CAMERA HANDLING. The far plane sits just past the model; the near plane wants
        // to sit just in front of it, but once the camera is inside the model's bounding sphere
        // `viewDist - modelR` goes to zero and below, which is not a perspective frustum at all.
        // The floor that stood here was an absolute 1 cm, which made the depth camera's
        // behaviour depend on the model's ABSOLUTE world scale: a 0.15-radius shoulder pad and
        // a 12-radius boss ended up with wildly different f/n at the same relative zoom. A
        // floor expressed as a fraction of the far plane is scale-free and caps f/n at 100.
        //
        // Precision is NOT the reason. This buffer is 24-bit (see viewDepth's construction), so
        // even in the worst case above the depth quantum at the model is a few parts in 1e6 of
        // a model radius, against a self-hit guard of 1e-2 R -- three orders of magnitude of
        // headroom.
        float depthFar = viewDist + modelR;
        depthCam.nearClipPlane = Mathf.Max(viewDist - modelR, depthFar * 0.01f);
        // A degenerate bounds (a single point, or a model behind the camera after a pan) can
        // leave far at or below near, which produces a singular projection and a buffer full of
        // NaN. Cheap to rule out; impossible to diagnose from the picture if it ever happens.
        depthCam.farClipPlane = Mathf.Max(depthFar, depthCam.nearClipPlane * 1.001f);
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

        // The planes the shader needs to undo the projection, plus the two products it would
        // otherwise recompute on each of the march's samples. Read back OFF THE CAMERA
        // rather than from the locals above: Unity clamps the nearClipPlane setter, and the
        // shader's n and f have to be the ones the projection matrix was actually built from or
        // the inversion is undone with the wrong constants.
        float dNear = depthCam.nearClipPlane;
        float dFar = depthCam.farClipPlane;
        Shader.SetGlobalVector("_WmvViewDepthParams",
                               new Vector4(dNear, dFar, dFar - dNear, dNear * dFar));

        // Self-hit guard and thickness, in WORLD UNITS.
        //
        // These used to be divided by a `depthRange` of 2 model radii, on the assumption that the
        // pinched near/far made [0,1] depth proportional to world distance. Perspective depth is
        // hyperbolic, so that held only when the camera was many radii away: closer in, the same
        // physical gap produced a steadily smaller depth difference until it fell under the guard
        // and the effect switched off, and the error also varied across the image so the window
        // was far too permissive on the far side of the model. The shader now linearises the
        // stored depth (WmvLinearViewDepth), so a metre is a metre and these are simply the two
        // distances they were always meant to be.
        //
        // THICKNESS IS THE KNIFE-EDGE OF THIS TECHNIQUE. A first version assumed occluders
        // 0.20 R thick, and the result was a faint even wash over every large surface: any
        // geometry anywhere within a fifth of the model IN FRONT of the ray -- the far side of
        // a fold, the silhouette of the cloak -- counted as touching. Contact shadows are about
        // the near field, so the assumed thickness is a third of the march's own reach (0.08 R
        // against CONTACT_RANGE's 0.25 R): an occluder matters only if the ray passes within
        // touching distance BEHIND it. The guard against a fragment finding its own surface is
        // ~1 % of the model, paired with the ray's starting push off the surface in the shader.
        // Both numbers are unchanged -- only the space they are measured in is.
        Shader.SetGlobalFloat("_WmvContactEps", ContactBias * modelR);
        Shader.SetGlobalFloat("_WmvContactThick", ContactThickness * modelR);

        // The rest of the controls, straight through. Published every frame rather than on
        // receipt because these are global shader state and this is the one place that owns it;
        // a value set once could be lost to any other code that touches the same globals, and a
        // slider that stops working intermittently is worse than no slider.
        Shader.SetGlobalFloat("_WmvContactStrength", ContactStrength);
        Shader.SetGlobalFloat("_WmvContactReach", ContactReach);
        Shader.SetGlobalFloat("_WmvContactSoftness", ContactSoftness);
        Shader.SetGlobalFloat("_WmvContactSteps", ContactSteps);
        Shader.SetGlobalFloat("_WmvContactTaps", ContactTaps);
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
