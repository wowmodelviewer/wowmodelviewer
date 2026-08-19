// WmvOrbitCamera.cs
//
// Simple orbit/zoom/pan controls for the embedded viewport:
//   left-drag  = orbit around the pivot
//   right-drag = pan the pivot
//   wheel      = zoom
// Attached to the main camera by WmvMain.

using UnityEngine;

public class WmvOrbitCamera : MonoBehaviour
{
    public Vector3 pivot = Vector3.zero;
    public float distance = 4f;
    public float yaw = 0f;
    public float pitch = 20f;

    const float OrbitSpeed = 0.25f;   // degrees per pixel
    const float PanSpeed = 0.0025f;   // world units per pixel per distance
    const float ZoomSpeed = 0.12f;
    const float MinDistance = 0.2f;
    const float MaxDistance = 200f;

    Vector3 lastMouse;

    void Start()
    {
        Apply();
    }

    void Update()
    {
        var mouse = Input.mousePosition;
        var delta = mouse - lastMouse;
        lastMouse = mouse;

        bool changed = false;

        if (Input.GetMouseButton(0) && !Input.GetMouseButtonDown(0))
        {
            yaw += delta.x * OrbitSpeed;
            pitch = Mathf.Clamp(pitch - delta.y * OrbitSpeed, -89f, 89f);
            changed = true;
        }
        else if (Input.GetMouseButton(1) && !Input.GetMouseButtonDown(1))
        {
            var t = transform;
            pivot -= t.right * (delta.x * PanSpeed * distance);
            pivot -= t.up * (delta.y * PanSpeed * distance);
            changed = true;
        }

        var scroll = Input.GetAxis("Mouse ScrollWheel");
        if (Mathf.Abs(scroll) > 0.0001f)
        {
            distance = Mathf.Clamp(distance * (1f - scroll * ZoomSpeed * 10f), MinDistance, MaxDistance);
            changed = true;
        }

        if (changed)
            Apply();
    }

    void Apply()
    {
        var rot = Quaternion.Euler(pitch, yaw, 0f);
        transform.position = pivot - rot * Vector3.forward * distance;
        transform.rotation = rot;
    }
}
