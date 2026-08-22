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

using UnityEngine;
#if !ENABLE_LEGACY_INPUT_MANAGER && ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

public class WmvOrbitCamera : MonoBehaviour
{
    public Vector3 pivot = Vector3.zero;
    public float distance = 4f;
    public float yaw = 30f;
    public float pitch = 15f;

    const float OrbitSpeed = 0.25f;    // degrees per pixel
    const float PanSpeed = 0.0025f;    // world units per pixel, scaled by distance
    const float ZoomSpeed = 0.12f;
    const float MinDistance = 0.05f;
    const float MaxDistance = 500f;

    Vector3 lastMouse;
    bool warnedNoInput;

    void Start() { Apply(); }

    /// <summary>
    /// Point the camera at a model's bounds from a three-quarter angle, far enough back that
    /// the whole thing fits the vertical field of view with a small margin.
    /// </summary>
    public void Frame(Bounds bounds)
    {
        pivot = bounds.center;

        float radius = bounds.extents.magnitude;
        if (radius <= 0.0001f) radius = 1f;

        var cam = GetComponent<Camera>();
        float fov = (cam != null ? cam.fieldOfView : 60f) * Mathf.Deg2Rad;
        distance = Mathf.Clamp(radius / Mathf.Max(0.05f, Mathf.Sin(fov * 0.5f)) * 1.25f,
                               MinDistance, MaxDistance);

        // Keep near/far sane for models that span a few centimetres or a few hundred metres.
        if (cam != null)
        {
            cam.nearClipPlane = Mathf.Max(0.01f, distance * 0.01f);
            cam.farClipPlane = Mathf.Max(100f, distance * 20f);
        }

        yaw = 30f;
        pitch = 15f;
        Apply();
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
        scroll = Input.GetAxis("Mouse ScrollWheel");
        return true;
#elif ENABLE_INPUT_SYSTEM
        var mouse = Mouse.current;
        if (mouse == null)
            return false;
        Vector2 p = mouse.position.ReadValue();
        position = new Vector3(p.x, p.y, 0f);
        leftHeld = mouse.leftButton.isPressed;
        rightHeld = mouse.rightButton.isPressed;
        // The legacy axis is normalised to roughly +/-0.1 per notch; the Input System reports
        // raw scroll units (120 per notch on Windows). Scale so both feel the same.
        scroll = mouse.scroll.ReadValue().y / 1200f;
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

    void Update()
    {
        Vector3 mouse;
        bool leftHeld, rightHeld;
        float scroll;
        if (!ReadMouse(out mouse, out leftHeld, out rightHeld, out scroll))
            return;

        var delta = mouse - lastMouse;
        bool hadMouse = lastMouse != Vector3.zero;
        lastMouse = mouse;
        if (!hadMouse)
            return;    // first frame: no meaningful delta yet

        bool changed = false;
        if (leftHeld)
        {
            yaw += delta.x * OrbitSpeed;
            pitch = Mathf.Clamp(pitch - delta.y * OrbitSpeed, -89f, 89f);
            changed = true;
        }
        else if (rightHeld)
        {
            pivot -= transform.right * (delta.x * PanSpeed * distance);
            pivot -= transform.up * (delta.y * PanSpeed * distance);
            changed = true;
        }

        if (Mathf.Abs(scroll) > 0.0001f)
        {
            distance = Mathf.Clamp(distance * (1f - scroll * ZoomSpeed * 10f), MinDistance, MaxDistance);
            changed = true;
        }

        if (changed) Apply();
    }

    void Apply()
    {
        var rot = Quaternion.Euler(pitch, yaw, 0f);
        transform.position = pivot - rot * Vector3.forward * distance;
        transform.rotation = rot;
    }
}
