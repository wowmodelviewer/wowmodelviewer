// WmvFlyCamera.cs
//
// THE FREE CAMERA of the world view (protocol 15, docs/world-viewer/05-maps-viewer-ui.md section 21). The Maps viewer's
// Enter world puts it at a position in the world (setWorldCamera, or loadTerrainTile's camera) and the user flies from
// there. It holds the main camera only while the world view gives it (WmvTerrainViewMode.ApplyFly); the orbit camera is
// off meanwhile and gets the camera back, exactly, when the world view is left or the overview is chosen.
//
// CONTROLS -- the mouse alone is enough:
//   left or right drag      look around (yaw, pitch; pitch held within +-89 degrees)
//   wheel                   fly forwards / backwards along the view: a quarter second at the current speed per notch
//   W A S D, arrow keys     forwards, left, backwards, right    } once a press in the viewport has given it the keys
//   E or Space, Q or C      up, down (the world's vertical)     }
//   Shift, Alt              four times faster, a quarter as fast
//
// INPUT, as WmvOrbitCamera's header measured it for a player embedded in the host's window: position and buttons from the
// legacy Input Manager, the wheel from the Input System. Buttons and the wheel are device state, seen even over the
// host's other panels, so a drag counts only when its press started over the viewport, and the wheel only over it. The
// keys are read from both backends; whether they reach a player embedded in another process's window is measured in
// 05-maps-viewer-ui.md, not assumed. They count only after a press inside the viewport and until a press outside it -- the
// player has no other sign of having the keyboard -- so typing in Browse's search box never moves the camera.
//
// STEP. Everything that moves the camera is a pure function (Step) of the state, one frame's input and the frame time,
// so the self-test drives it without input devices.

using UnityEngine;
#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

public class WmvFlyCamera : MonoBehaviour
{
    /// <summary>Yards per second at the start (flying speed in the game is a few times running speed, about 7).</summary>
    public const float DefaultSpeed = 30f;
    public const float LookDegreesPerPixel = 0.2f;
    public const float MaxPitch = 89f;
    public const float WheelSeconds = 0.25f;
    public const float FastFactor = 4f, SlowFactor = 0.25f;
    /// <summary>Clip planes while flying: near enough for a wall at arm's length, far enough for a tile and its neighbours.</summary>
    public const float NearPlane = 0.25f, FarPlane = 3000f;

    public struct State
    {
        public Vector3 Position;     // Unity space
        public float Yaw;            // degrees clockwise from north (Unity +Z), as Unity's Euler y
        public float Pitch;          // degrees, up positive
        public float Speed;          // yards per second
    }

    public struct FlyInput
    {
        public float LookX, LookY;   // pixels the pointer moved this frame while a drag is on (y up positive)
        public float Forward, Right, Up;   // -1, 0 or 1 each
        public float WheelNotches;
        public float SpeedFactor;    // 1, FastFactor or SlowFactor
    }

    /// <summary>The camera's rotation for a yaw and pitch.</summary>
    public static Quaternion Rotation(float yaw, float pitch)
    {
        return Quaternion.Euler(-pitch, yaw, 0f);
    }

    /// <summary>One frame: look first, then move along the new view (forwards and right) and the world's vertical.</summary>
    public static State Step(State s, FlyInput i, float dt)
    {
        s.Yaw = Mathf.Repeat(s.Yaw + i.LookX * LookDegreesPerPixel, 360f);
        s.Pitch = Mathf.Clamp(s.Pitch + i.LookY * LookDegreesPerPixel, -MaxPitch, MaxPitch);
        Quaternion rot = Rotation(s.Yaw, s.Pitch);
        float speed = s.Speed * (i.SpeedFactor > 0f ? i.SpeedFactor : 1f);
        Vector3 forward = rot * Vector3.forward, right = rot * Vector3.right;
        Vector3 move = (forward * i.Forward + right * i.Right + Vector3.up * i.Up) * (speed * Mathf.Max(0f, dt));
        move += forward * (i.WheelNotches * speed * WheelSeconds);
        s.Position += move;
        return s;
    }

    State state;
    Vector3 lastMouse;
    bool haveLastMouse, leftWasHeld, rightWasHeld, dragFromViewport, keysGiven;

    public State Current { get { return state; } }

    /// <summary>Put the camera at a Unity position, facing yaw and pitch (degrees); the speed starts over, the keys wait for
    /// a press in the viewport.</summary>
    public void Place(Vector3 position, float yaw, float pitch)
    {
        state = new State { Position = position, Yaw = Mathf.Repeat(yaw, 360f), Pitch = Mathf.Clamp(pitch, -MaxPitch, MaxPitch), Speed = DefaultSpeed };
        haveLastMouse = false;
        dragFromViewport = false;
        keysGiven = false;
        ApplyToTransform();
    }

    void ApplyToTransform()
    {
        transform.position = state.Position;
        transform.rotation = Rotation(state.Yaw, state.Pitch);
    }

    static bool PointerOverViewport(Vector3 p)
    {
        return p.x >= 0f && p.x <= Screen.width && p.y >= 0f && p.y <= Screen.height;
    }

    void Update()
    {
        Vector3 mouse;
        bool leftHeld, rightHeld;
        float wheel;
        ReadMouse(out mouse, out leftHeld, out rightHeld, out wheel);
        bool over = PointerOverViewport(mouse);

        var i = new FlyInput { SpeedFactor = 1f };
        bool pressed = (leftHeld && !leftWasHeld) || (rightHeld && !rightWasHeld);
        if (pressed)
        {
            dragFromViewport = over;
            keysGiven = over;
        }
        if (!leftHeld && !rightHeld)
            dragFromViewport = false;
        leftWasHeld = leftHeld;
        rightWasHeld = rightHeld;
        if (haveLastMouse && dragFromViewport && (leftHeld || rightHeld))
        {
            Vector3 d = mouse - lastMouse;
            i.LookX = d.x;
            i.LookY = d.y;
        }
        lastMouse = mouse;
        haveLastMouse = true;
        if (over && Mathf.Abs(wheel) > 0.0001f)
            i.WheelNotches = wheel;
        if (keysGiven)
            ReadKeys(ref i);
        if (i.LookX == 0f && i.LookY == 0f && i.Forward == 0f && i.Right == 0f && i.Up == 0f && i.WheelNotches == 0f)
            return;
        state = Step(state, i, Time.unscaledDeltaTime);
        ApplyToTransform();
    }

    static void ReadMouse(out Vector3 position, out bool leftHeld, out bool rightHeld, out float wheel)
    {
        position = Vector3.zero;
        leftHeld = rightHeld = false;
        wheel = 0f;
#if ENABLE_LEGACY_INPUT_MANAGER
        position = UnityEngine.Input.mousePosition;
        leftHeld = UnityEngine.Input.GetMouseButton(0);
        rightHeld = UnityEngine.Input.GetMouseButton(1);
#elif ENABLE_INPUT_SYSTEM
        var m = Mouse.current;
        if (m != null)
        {
            Vector2 p = m.position.ReadValue();
            position = new Vector3(p.x, p.y, 0f);
            leftHeld = m.leftButton.isPressed;
            rightHeld = m.rightButton.isPressed;
        }
#endif
#if ENABLE_INPUT_SYSTEM
        var mouse = Mouse.current;
        if (mouse != null)
        {
            float y = mouse.scroll.ReadValue().y;
            if (Mathf.Abs(y) >= 10f)
                y /= 120f;
            wheel = y;
        }
#elif ENABLE_LEGACY_INPUT_MANAGER
        wheel = UnityEngine.Input.GetAxis("Mouse ScrollWheel") * 10f;
#endif
    }

    /// <summary>The movement keys, from whichever backend sees them.</summary>
    static void ReadKeys(ref FlyInput i)
    {
        bool w = false, a = false, s = false, d = false, up = false, down = false, fast = false, slow = false;
#if ENABLE_LEGACY_INPUT_MANAGER
        w = UnityEngine.Input.GetKey(KeyCode.W) || UnityEngine.Input.GetKey(KeyCode.UpArrow);
        s = UnityEngine.Input.GetKey(KeyCode.S) || UnityEngine.Input.GetKey(KeyCode.DownArrow);
        a = UnityEngine.Input.GetKey(KeyCode.A) || UnityEngine.Input.GetKey(KeyCode.LeftArrow);
        d = UnityEngine.Input.GetKey(KeyCode.D) || UnityEngine.Input.GetKey(KeyCode.RightArrow);
        up = UnityEngine.Input.GetKey(KeyCode.E) || UnityEngine.Input.GetKey(KeyCode.Space);
        down = UnityEngine.Input.GetKey(KeyCode.Q) || UnityEngine.Input.GetKey(KeyCode.C);
        fast = UnityEngine.Input.GetKey(KeyCode.LeftShift) || UnityEngine.Input.GetKey(KeyCode.RightShift);
        slow = UnityEngine.Input.GetKey(KeyCode.LeftAlt) || UnityEngine.Input.GetKey(KeyCode.RightAlt);
#endif
#if ENABLE_INPUT_SYSTEM
        var k = Keyboard.current;
        if (k != null)
        {
            w |= k.wKey.isPressed || k.upArrowKey.isPressed;
            s |= k.sKey.isPressed || k.downArrowKey.isPressed;
            a |= k.aKey.isPressed || k.leftArrowKey.isPressed;
            d |= k.dKey.isPressed || k.rightArrowKey.isPressed;
            up |= k.eKey.isPressed || k.spaceKey.isPressed;
            down |= k.qKey.isPressed || k.cKey.isPressed;
            fast |= k.shiftKey.isPressed;
            slow |= k.altKey.isPressed;
        }
#endif
        i.Forward = (w ? 1f : 0f) - (s ? 1f : 0f);
        i.Right = (d ? 1f : 0f) - (a ? 1f : 0f);
        i.Up = (up ? 1f : 0f) - (down ? 1f : 0f);
        i.SpeedFactor = fast ? FastFactor : slow ? SlowFactor : 1f;
    }
}
