// WmvOrbitCamera.cs
//
// Orbit / pan / zoom controls for the embedded viewport, plus bounds-driven framing so a newly
// loaded model is visible immediately without touching the camera:
//   left-drag  = orbit around the pivot
//   right-drag = pan the pivot
//   wheel      = zoom
//
// Frame() derives everything from the mesh bounds and the camera's own field of view -- there
// are no per-model constants, so a chicken and a dragon both land nicely in view.
//
// INPUT BACKENDS
//   The player must work in whatever project it is dropped into, so mouse reading is compiled
//   against whichever backend is active:
//
//     Active Input Handling = "Input Manager (Old)"  -> ENABLE_LEGACY_INPUT_MANAGER
//     Active Input Handling = "Input System (New)"   -> ENABLE_INPUT_SYSTEM
//     Active Input Handling = "Both"                 -> both defined; the legacy path is used
//
//   Reading UnityEngine.Input while only the new backend is active throws
//   InvalidOperationException *every frame*, which is what a plain Input.mousePosition call did
//   here. If neither backend is available the camera keeps framing models and simply does not
//   respond to the mouse, warning exactly once instead of once per frame.
//
//   THE WHEEL IS THE EXCEPTION, AND IT IS MEASURED. This player runs as a CHILD WINDOW of a
//   wxWidgets host, and in that arrangement the legacy Input Manager never sees a wheel notch:
//   it takes the wheel from WM_MOUSEWHEEL, which Windows delivers to the focused window of the
//   focused application -- and an embedded child of another process's frame is not that. Rolling
//   a real wheel over the viewport and logging both backends side by side, twelve notches gave
//   twelve readings of
//
//       legacy Input.GetAxis("Mouse ScrollWheel") = 0.00000
//       Input System Mouse.current.scroll.y       = 1.00
//
//   with and without a prior click in the pane, and neither the host nor a WM_MOUSEWHEEL posted
//   directly to the player's render window produced anything. Mouse POSITION and BUTTONS arrive
//   on the legacy backend (position is polled from the OS cursor, buttons were observed held),
//   so only the wheel has to come from the Input System's raw-input path. That is why the scroll
//   is read separately from everything else below.
//
//   ONE NOTCH IS 1.0 THERE, NOT 120. The previous code divided the Input System's reading by
//   1200 on the assumption that Windows reports 120 units per notch; measured here it reports
//   1.0, so that scaling was 120x too small and would have made the wheel look dead even on the
//   backend that receives it. Both shapes are handled below rather than assumed.

using UnityEngine;
// Present whenever the package is, NOT only when it is the sole backend: with "Both" active --
// which is what this project builds with -- the legacy manager supplies position and buttons
// while the wheel comes from here. See the wheel note in the file header.
#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

public class WmvOrbitCamera : MonoBehaviour
{
    public Vector3 pivot = Vector3.zero;
    public float distance = 4f;
    /// <summary>
    /// The default view is the model's FRONT. A WoW model faces +X in its own space, which the
    /// converter maps to Unity +Z (WowCoordinateConverter); a camera at yaw 0 sits on -Z looking
    /// down +Z, at the model's back -- and the old three-quarter default of 30 was that back
    /// from slightly to one side. The legacy OpenGL viewport looks at the front (its camera
    /// starts on the model's +X axis: OrbitCamera.cpp, yaw 0 / pitch 90), so yaw 180 here is
    /// what the app has always shown. Pitch stays a little above eye level.
    /// </summary>
    public const float FrontYaw = 180f;
    public const float DefaultPitch = 15f;

    public float yaw = FrontYaw;
    public float pitch = DefaultPitch;

    const float OrbitSpeed = 0.25f;    // degrees per pixel
    const float PanSpeed = 0.0025f;    // world units per pixel, scaled by distance

    /// <summary>
    /// What one wheel notch multiplies the orbit distance by. Taken from the legacy OpenGL
    /// viewport, which uses exactly this (modelcanvas.cpp:474, radius * powf(0.82f, notches)) --
    /// so the two viewports feel the same under the hand.
    ///
    /// MULTIPLYING is the point. A fixed step per notch is unusable at both ends: it crawls when
    /// a WMO sits hundreds of units away and it jumps straight through a 0.2-unit item component.
    /// Scaling makes a notch mean the same PROPORTION of the current distance everywhere, which
    /// is what "zoom speed scales with distance" actually asks for. Raising it to a power of the
    /// notch count also keeps it exactly symmetric: one notch in and one notch out returns the
    /// camera to where it started, which `distance * (1 - k)` followed by `distance * (1 + k)`
    /// does not (it loses k^2 every round trip).
    /// </summary>
    const float ZoomPerNotch = 0.82f;

    /// <summary>
    /// How fast the eased distance chases the wheel, as an exponential rate per second. Used as
    /// 1 - exp(-k * dt) rather than a bare Lerp with a constant factor, so the motion is the same
    /// whether the viewport is running at 30 fps or 300. At 16 it covers ~95 % in 190 ms: quick
    /// enough to feel like a direct response, slow enough to read as a glide.
    /// </summary>
    const float ZoomSmoothing = 16f;

    /// <summary>
    /// The zoom range, as multiples of the distance the model was FRAMED at. Derived from the
    /// model rather than fixed, because a fixed range cannot serve both ends of this client: the
    /// legacy viewport's hardcoded floor of 0.5 units (OrbitCamera.cpp:12) is further away than
    /// a 0.3-unit item component is wide, so such a model can never be zoomed into at all.
    /// Framing distance is itself proportional to the model's radius, so these two factors give
    /// every model the same usable range in proportion to its own size.
    /// </summary>
    const float MinDistanceFactor = 0.02f;
    const float MaxDistanceFactor = 20f;

    /// <summary>An absolute floor, so a degenerate bounds cannot put the camera AT the pivot.
    /// The camera can approach the focus point but never reach or pass through it.</summary>
    const float AbsoluteMinDistance = 1e-3f;

    /// <summary>
    /// Where the wheel is taking the camera. `distance` eases toward this every frame; the wheel
    /// only ever writes here. Keeping the two apart is what makes the motion smooth without
    /// making it laggy -- the target is reached instantly, the eye gets there over ~190 ms.
    /// </summary>
    float targetDistance = 4f;

    float framedDistance = 4f;
    float minDistance = AbsoluteMinDistance;
    float maxDistance = 500f;

    Vector3 lastMouse;
    bool warnedNoInput;
    bool leftWasHeld, rightWasHeld;
    bool dragFromViewport;      // the current press began over the player's own client area

    void Start()
    {
        targetDistance = distance;
        Apply();
    }

    /// <summary>
    /// A WORLD MODEL'S VIEW. A building or a cave has no "front" the way a creature does, and seen
    /// level from yaw 180 most of one is a wall; a three-quarter view from 30 degrees above the
    /// horizon shows its footprint and two sides. It is also the audit's OpenGL reference view A
    /// (archived camera yaw 45, pitch 60 -- 30 degrees up -- which maps to this yaw and pitch through
    /// the coordinate converter), so the default picture can be laid beside those captures.
    /// </summary>
    public const float MapObjectYaw = 135f;
    public const float MapObjectPitch = 30f;

    /// <summary>
    /// How far a world model can be zoomed in, as a fraction of its framing distance. The model's
    /// 2 % is too far out for a building: the orbit pivot is the middle of the object, and 2 % of a
    /// 10,000-unit cave's framing distance keeps the camera 500 units from it. 0.1 % lets the camera
    /// reach a wall (panning moves the pivot, and pan speed scales with distance).
    /// </summary>
    const float MapObjectMinDistanceFactor = 0.001f;

    /// <summary>True while a world model is framed: its clip planes follow its size as well as the
    /// distance (see UpdateClipPlanes). Frame() for a model turns it off again.</summary>
    bool mapObjectPlanes;
    float framedRadius = 1f;
    /// <summary>Centre of the framed bounds. The pivot starts there but a pan moves it without limit,
    /// so the world-model far plane is measured from the object itself (see UpdateClipPlanes).</summary>
    Vector3 framedCenter = Vector3.zero;

    /// <summary>
    /// Point the camera at a model's bounds from the front, a little above, far enough back that
    /// the whole thing fits the vertical field of view with a small margin.
    /// </summary>
    public void Frame(Bounds bounds)
    {
        FrameWith(bounds, false);
    }

    /// <summary>
    /// Frame a world model: the same bounds rule as Frame, from the map-object view, with a closer
    /// zoom floor and clip planes that cover the whole object (see UpdateClipPlanes). A later Frame()
    /// for a model restores everything a model had.
    /// </summary>
    public void FrameMapObject(Bounds bounds)
    {
        FrameWith(bounds, true);
    }

    /// <summary>
    /// Re-aim the framed camera from a given angle, at a multiple of the framing distance (clamped to
    /// the zoom range), settled at once; a distanceScale of 0 or less keeps the current distance. For
    /// the WMV_VIEWPORT_ORBIT capture hook and the runtime self-test only: nothing in normal viewing
    /// calls it.
    /// </summary>
    public void SetView(float newYaw, float newPitch, float distanceScale)
    {
        yaw = newYaw;
        pitch = Mathf.Clamp(newPitch, -89f, 89f);
        if (distanceScale > 0f)
            distance = targetDistance = Mathf.Clamp(framedDistance * distanceScale, minDistance, maxDistance);
        Apply();
    }

    /// <summary>Whether the clip planes currently follow a framed world model, for the self-test and the log.</summary>
    public bool MapObjectPlanes { get { return mapObjectPlanes; } }

    void FrameWith(Bounds bounds, bool mapObject)
    {
        pivot = bounds.center;
        mapObjectPlanes = mapObject;

        float radius = bounds.extents.magnitude;
        if (radius <= 0.0001f) radius = 1f;
        framedRadius = radius;
        framedCenter = bounds.center;

        var cam = GetComponent<Camera>();
        float fov = (cam != null ? cam.fieldOfView : 60f) * Mathf.Deg2Rad;
        // The narrower half-angle of the two: fieldOfView is the VERTICAL one, and a viewport
        // taller than it is wide -- as the viewport becomes with wide panels docked beside it, and
        // as the old side pane always was -- has a smaller horizontal angle, which is what clipped the sides of a wide model that
        // "fit" vertically. Still one rule from the bounds and the camera, nothing per model.
        float halfV = fov * 0.5f;
        float aspect = (cam != null && cam.aspect > 0.001f) ? cam.aspect : 1f;
        float halfH = Mathf.Atan(Mathf.Tan(halfV) * aspect);
        framedDistance = radius / Mathf.Max(0.05f, Mathf.Sin(Mathf.Min(halfV, halfH))) * 1.25f;

        // THE ZOOM RANGE COMES FROM THE MODEL. See MinDistanceFactor.
        minDistance = Mathf.Max(framedDistance * (mapObject ? MapObjectMinDistanceFactor : MinDistanceFactor),
                                AbsoluteMinDistance);
        maxDistance = framedDistance * MaxDistanceFactor;

        distance = Mathf.Clamp(framedDistance, minDistance, maxDistance);
        targetDistance = distance;          // a new model starts settled, not gliding

        yaw = mapObject ? MapObjectYaw : FrontYaw;
        pitch = mapObject ? MapObjectPitch : DefaultPitch;
        Apply();
    }

    /// <summary>
    /// Near and far, derived from the CURRENT distance.
    ///
    /// This has to follow the zoom, not just the framing. The planes used to be set once, from
    /// the framing distance, which meant zooming in past a fiftieth of it put the near plane in
    /// front of the model and sliced it away -- exactly the range the new minimum opens up. Same
    /// formulas as before, evaluated whenever the distance changes instead of only once.
    /// </summary>
    void UpdateClipPlanes(Camera cam)
    {
        if (cam == null)
            return;
        if (!mapObjectPlanes)
        {
            cam.nearClipPlane = Mathf.Max(0.001f, distance * 0.01f);
            cam.farClipPlane = Mathf.Max(100f, distance * 20f);
            return;
        }
        // A WORLD MODEL. The model rule ties far to the distance alone, which is right while the
        // camera is outside the object -- and a world model is where the camera goes INSIDE: zoomed to
        // a wall of a 10,000-unit cave, 20x the distance to the pivot cuts away most of the cave behind
        // it. Far therefore also reaches past the far side of the whole object: the camera's distance to
        // the CENTRE OF THE FRAMED BOUNDS plus their radius, with a 20 % margin, so a 15,000-unit WMO
        // stays whole at every zoom and there is no fixed ceiling like the old 6400 far plane. It is
        // measured from the object, not from the pivot, because a right-drag pans the pivot without
        // limit: zoomed in on a pivot panned off-centre, distance + radius falls short of the far side
        // and a hard cut sweeps across the object while orbiting. Apply has already placed the camera.
        // Near keeps the model's 1 % of the distance, which is what gives a small WMO its precision,
        // but never drops below 1/10000 of far: that ratio keeps the depth buffer meaningful across the
        // whole range when the camera is close to a huge object.
        float reach = Vector3.Distance(cam.transform.position, framedCenter) + framedRadius;
        float far = Mathf.Max(Mathf.Max(100f, distance * 20f), reach * 1.2f);
        cam.farClipPlane = far;
        cam.nearClipPlane = Mathf.Max(Mathf.Max(0.001f, distance * 0.01f), far * 1e-4f);
    }

    // ---------------------------------------------------------------- input backends

    /// <summary>True when a mouse could be read at all (false disables orbit input silently).</summary>
    bool ReadMouse(out Vector3 position, out bool leftHeld, out bool rightHeld, out float scroll)
    {
        position = Vector3.zero;
        leftHeld = rightHeld = false;
        scroll = 0f;

#if ENABLE_LEGACY_INPUT_MANAGER
        position = Input.mousePosition;
        leftHeld = Input.GetMouseButton(0);
        rightHeld = Input.GetMouseButton(1);
        // NOT Input.GetAxis("Mouse ScrollWheel") -- see the wheel note in the file header. It
        // reads 0 in this embedding, every frame, however hard the wheel is rolled.
        scroll = ReadWheelNotches();
        return true;
#elif ENABLE_INPUT_SYSTEM
        var mouse = Mouse.current;
        if (mouse == null)
            return false;
        Vector2 p = mouse.position.ReadValue();
        position = new Vector3(p.x, p.y, 0f);
        leftHeld = mouse.leftButton.isPressed;
        rightHeld = mouse.rightButton.isPressed;
        scroll = ReadWheelNotches();
        return true;
#else
        if (!warnedNoInput)
        {
            warnedNoInput = true;
            Debug.LogWarning("WMV: no input backend is available (neither the legacy Input " +
                             "Manager nor the Input System package) -- camera orbit is disabled. " +
                             "Models are still framed automatically.");
        }
        return false;
#endif
    }

    /// <summary>
    /// This frame's wheel movement, in NOTCHES -- one detent of a normal mouse wheel is 1.
    ///
    /// Read from the Input System wherever it is compiled in, because that is the only backend
    /// that receives a wheel event in this embedding (the file header has the measurement). The
    /// legacy axis is the fallback for a build without the Input System package, where it is the
    /// only thing there is; it reports 0.1 per notch, so it is scaled to notches to match.
    ///
    /// The Input System reports 1.0 per notch here and 120 per notch in some other Unity/platform
    /// combinations. Rather than pick one and be wrong on the other machine, anything large is
    /// treated as raw units and divided down -- no real mouse produces ten detents in one frame.
    /// </summary>
    float ReadWheelNotches()
    {
#if ENABLE_INPUT_SYSTEM
        var m = Mouse.current;
        if (m != null)
        {
            float y = m.scroll.ReadValue().y;
            if (Mathf.Abs(y) >= 10f)
                y /= 120f;
            return y;
        }
#endif
#if ENABLE_LEGACY_INPUT_MANAGER
        // 0.1 per notch, from the project's own axis settings (sensitivity 0.1).
        return Input.GetAxis("Mouse ScrollWheel") * 10f;
#else
        return 0f;
#endif
    }

    /// <summary>
    /// Is the pointer actually over the viewport?
    ///
    /// The wheel is read from a device, not from a window message, so it keeps arriving while the
    /// pointer is over WMV's other panels. Zooming the model because the user scrolled a list
    /// next to it would be wrong, so the notch is only taken when the pointer is inside the
    /// player's own client area -- which is what "scrolling over the viewport controls zoom"
    /// means. Orbit and pan use it too, once per press: see Update.
    /// </summary>
    static bool PointerOverViewport(Vector3 position)
    {
        return position.x >= 0f && position.x <= Screen.width &&
               position.y >= 0f && position.y <= Screen.height;
    }

    void Update()
    {
        Vector3 mouse;
        bool leftHeld, rightHeld;
        float scroll;
        if (!ReadMouse(out mouse, out leftHeld, out rightHeld, out scroll))
            return;

        bool changed = false;

        // ---- ZOOM, BEFORE THE DRAG GATE ------------------------------------------------
        // A notch needs no previous cursor position, so it must not be thrown away with the
        // first frame's meaningless delta -- nor on any later frame where the OS happens to put
        // the cursor at exactly (0,0), which the `lastMouse != Vector3.zero` test below reads as
        // "no previous sample" all over again.
        if (Mathf.Abs(scroll) > 0.0001f && PointerOverViewport(mouse))
            ZoomByNotches(scroll);

        // Ease toward the target every frame. Runs whether or not the wheel moved, so a notch
        // keeps gliding after the user stops rolling.
        if (AdvanceZoom(Time.unscaledDeltaTime))
            changed = true;

        var delta = mouse - lastMouse;
        bool hadMouse = lastMouse != Vector3.zero;
        lastMouse = mouse;
        if (!hadMouse)
        {
            if (changed) Apply();
            return;    // first frame: no meaningful DRAG delta yet
        }

        // ORBIT AND PAN ONLY FROM A PRESS THAT STARTED OVER THE VIEWPORT. The buttons are read
        // from the device, like the wheel, so a press made on the file list or a slider beside
        // the viewport reads as held here too -- and the pointer's travel to that control became
        // an orbit or, worse, a pan that carried the pivot away from the model just framed. The
        // gate is decided once, on the frame the button goes down, and holds for that press, so
        // a drag that starts inside and wanders outside keeps working.
        if ((leftHeld && !leftWasHeld) || (rightHeld && !rightWasHeld))
            dragFromViewport = PointerOverViewport(mouse);
        if (!leftHeld && !rightHeld)
            dragFromViewport = false;
        leftWasHeld = leftHeld;
        rightWasHeld = rightHeld;

        if (leftHeld && dragFromViewport)
        {
            yaw += delta.x * OrbitSpeed;
            pitch = Mathf.Clamp(pitch - delta.y * OrbitSpeed, -89f, 89f);
            changed = true;
        }
        else if (rightHeld && dragFromViewport)
        {
            pivot -= transform.right * (delta.x * PanSpeed * distance);
            pivot -= transform.up * (delta.y * PanSpeed * distance);
            changed = true;
        }

        if (changed) Apply();
    }

    /// <summary>
    /// Move the zoom TARGET by a number of wheel notches. Positive is towards the model.
    ///
    /// Public because the runtime self-test drives it directly: the wheel itself cannot be
    /// synthesised from a test, and a test that re-implemented this arithmetic would be checking
    /// its own copy rather than the code the wheel runs.
    /// </summary>
    public void ZoomByNotches(float notches)
    {
        targetDistance = Mathf.Clamp(targetDistance * Mathf.Pow(ZoomPerNotch, notches),
                                     minDistance, maxDistance);
    }

    /// <summary>
    /// Ease the live distance toward the target by one frame of dt. Returns true when it moved.
    ///
    /// 1 - exp(-k*dt) rather than a fixed Lerp factor: the fixed form converges at a rate that
    /// depends on the frame rate, so the same wheel gesture would glide differently on a fast
    /// machine and a slow one.
    /// </summary>
    public bool AdvanceZoom(float dt)
    {
        if (Mathf.Approximately(distance, targetDistance))
            return false;
        if (dt <= 0f)
            return false;
        float k = 1f - Mathf.Exp(-ZoomSmoothing * dt);
        distance = Mathf.Lerp(distance, targetDistance, k);
        // Land exactly rather than creeping asymptotically for the rest of the session.
        if (Mathf.Abs(distance - targetDistance) <= targetDistance * 1e-4f)
            distance = targetDistance;
        return true;
    }

    /// <summary>The zoom range this model was given, for the self-test and the log.</summary>
    public float MinDistance { get { return minDistance; } }
    public float MaxDistance { get { return maxDistance; } }
    public float FramedDistance { get { return framedDistance; } }
    public float TargetDistance { get { return targetDistance; } }

    void Apply()
    {
        // The pivot and the angles are untouched by zoom: only the radius along the same look
        // direction changes, so the camera cannot flip and the orbit target stays put. distance
        // is clamped above AbsoluteMinDistance, so it can approach the focus point but never
        // reach or pass through it.
        var rot = Quaternion.Euler(pitch, yaw, 0f);
        transform.position = pivot - rot * Vector3.forward * distance;
        transform.rotation = rot;
        UpdateClipPlanes(GetComponent<Camera>());
    }
}
