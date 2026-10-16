// WmvInputTrace.cs
//
// THE KEYBOARD PATH, TRACED (-wmvInputTrace on the player's command line; the host passes it when it is started with
// -inputtrace). For the free camera's keys (docs/world-viewer/05-maps-viewer-ui.md, section 22): which window has the
// keyboard, which keyboard and focus messages reach this player's window, which key states the two input backends then
// report, and where the free camera is. One line per change -- never one per frame while nothing changes -- with the
// wall clock, so a manual test leaves a record in unityRenderer.log that lines up with the host's log.
//
// How it sees:
//   - GetFocus / GetActiveWindow / GetForegroundWindow from the main thread. A child window of another process's window
//     shares that window's input queue, so GetFocus answers for the host's windows too ("host", "player", or none).
//   - Two thread hooks on the player window's thread: WH_GETMESSAGE for what is taken from the queue (keyboard messages,
//     button presses, WM_INPUT by device kind) and WH_CALLWNDPROC for what is sent (focus and activation). They only
//     record; the messages go on unchanged.
//   - Input.GetKey (the legacy Input Manager) and Keyboard.current (the Input System), the keys the free camera reads.
//
// Win32 only; elsewhere, and without the argument, nothing is installed.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;
#if ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
#endif

public class WmvInputTrace : MonoBehaviour
{
    /// <summary>The free camera's state for the trace (set by WmvMain): "" while it is off.</summary>
    public static Func<string> CameraLine;

    /// <summary>Is the argument on the command line?</summary>
    public static bool Requested()
    {
        foreach (string a in Environment.GetCommandLineArgs())
            if (a == "-wmvInputTrace")
                return true;
        return false;
    }

#if UNITY_STANDALONE_WIN && !UNITY_EDITOR
    delegate IntPtr HookProc(int code, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    struct MSG { public IntPtr hwnd; public uint message; public IntPtr wParam; public IntPtr lParam; public uint time; public int ptX, ptY; }
    [StructLayout(LayoutKind.Sequential)]
    struct CWPSTRUCT { public IntPtr lParam; public IntPtr wParam; public uint message; public IntPtr hwnd; }
    [StructLayout(LayoutKind.Sequential)]
    struct RAWINPUTHEADER { public uint dwType; public uint dwSize; public IntPtr hDevice; public IntPtr wParam; }

    [DllImport("user32.dll")] static extern IntPtr SetWindowsHookEx(int idHook, HookProc lpfn, IntPtr hMod, uint dwThreadId);
    [DllImport("user32.dll")] static extern bool UnhookWindowsHookEx(IntPtr hhk);
    [DllImport("user32.dll")] static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern IntPtr GetFocus();
    [DllImport("user32.dll")] static extern IntPtr GetActiveWindow();
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")] static extern bool IsChild(IntPtr parent, IntPtr child);
    [DllImport("user32.dll")] static extern IntPtr GetParent(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd, StringBuilder name, int max);
    [DllImport("user32.dll")] static extern uint GetRawInputData(IntPtr hRawInput, uint command, out RAWINPUTHEADER data, ref uint size, uint headerSize);
    [StructLayout(LayoutKind.Sequential)]
    struct RAWINPUTDEVICE { public ushort usUsagePage; public ushort usUsage; public uint dwFlags; public IntPtr hwndTarget; }
    [DllImport("user32.dll", SetLastError = true)]
    static extern uint GetRegisteredRawInputDevices([Out] RAWINPUTDEVICE[] devices, ref uint count, uint size);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("kernel32.dll")] static extern uint GetCurrentProcessId();

    const int WH_GETMESSAGE = 3, WH_CALLWNDPROC = 4;
    const uint WM_SETFOCUS = 0x0007, WM_KILLFOCUS = 0x0008, WM_ACTIVATE = 0x0006, WM_ACTIVATEAPP = 0x001C, WM_MOUSEACTIVATE = 0x0021,
               WM_INPUT = 0x00FF, WM_KEYDOWN = 0x0100, WM_KEYUP = 0x0101, WM_CHAR = 0x0102, WM_SYSKEYDOWN = 0x0104, WM_SYSKEYUP = 0x0105,
               WM_LBUTTONDOWN = 0x0201, WM_RBUTTONDOWN = 0x0204, WM_MBUTTONDOWN = 0x0207, RID_HEADER = 0x10000005;

    static WmvInputTrace instance;
    static HookProc getMessageProc, callWndProc;   // kept alive: the hooks call them
    static readonly object pendingLock = new object();
    static readonly List<string> pending = new List<string>();
    static int rawKeyboard, rawMouse;              // WM_INPUT messages taken, by device kind
    IntPtr getMessageHook, callWndHook;
    IntPtr playerWindow;
    uint playerThread, mainThread, processId;
    string lastFocus = "", lastKeys = "";
    float lastCameraLogTime;
    int lastRawKeyboard;

    void Start()
    {
        instance = this;
        processId = GetCurrentProcessId();
        mainThread = GetCurrentThreadId();
        playerWindow = FindPlayerWindow();
        uint pid;
        playerThread = playerWindow != IntPtr.Zero ? GetWindowThreadProcessId(playerWindow, out pid) : 0;
        if (playerThread != 0)
        {
            getMessageProc = OnGetMessage;
            callWndProc = OnCallWndProc;
            getMessageHook = SetWindowsHookEx(WH_GETMESSAGE, getMessageProc, IntPtr.Zero, playerThread);
            callWndHook = SetWindowsHookEx(WH_CALLWNDPROC, callWndProc, IntPtr.Zero, playerThread);
            Application.quitting += RemoveHooks;
        }
        Log(string.Format("on: player window {0} ({1}), parent {2}, its thread {3}, the main thread {4}; hooks {5} {6}",
                          Hex(playerWindow), ClassOf(playerWindow), Hex(playerWindow != IntPtr.Zero ? GetParent(playerWindow) : IntPtr.Zero),
                          playerThread, mainThread, getMessageHook != IntPtr.Zero ? "getmessage" : "(no getmessage)",
                          callWndHook != IntPtr.Zero ? "callwndproc" : "(no callwndproc)"));
    }

    void OnDestroy()
    {
        RemoveHooks();
        if (instance == this) instance = null;
    }

    /// <summary>The hooks removed -- when the trace goes and as the player quits (a hook left in place hangs the exit).</summary>
    void RemoveHooks()
    {
        if (getMessageHook != IntPtr.Zero) UnhookWindowsHookEx(getMessageHook);
        if (callWndHook != IntPtr.Zero) UnhookWindowsHookEx(callWndHook);
        getMessageHook = callWndHook = IntPtr.Zero;
    }

    /// <summary>The player's own window (WmvKeyboardFocus finds it: the child of the -parentHWND window of this process).</summary>
    IntPtr FindPlayerWindow()
    {
        return WmvKeyboardFocus.PlayerWindow;
    }

    static void Queue(string line)
    {
        lock (pendingLock)
        {
            if (pending.Count < 2000)
                pending.Add(DateTime.Now.ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture) + " " + line);
        }
    }

    static void Log(string line)
    {
        Debug.Log("WMV: [input] " + DateTime.Now.ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture) + " " + line);
    }

    static string Hex(IntPtr h) { return "0x" + h.ToInt64().ToString("X", CultureInfo.InvariantCulture); }

    static string ClassOf(IntPtr h)
    {
        if (h == IntPtr.Zero) return "none";
        var sb = new StringBuilder(128);
        GetClassName(h, sb, sb.Capacity);
        return sb.ToString();
    }

    static string KeyName(IntPtr vk)
    {
        int v = vk.ToInt32();
        if (v >= 0x41 && v <= 0x5A) return ((char)v).ToString();
        switch (v)
        {
            case 0x10: return "Shift";
            case 0x11: return "Ctrl";
            case 0x12: return "Alt";
            case 0x20: return "Space";
            case 0x25: return "Left";
            case 0x26: return "Up";
            case 0x27: return "Right";
            case 0x28: return "Down";
            case 0x0D: return "Enter";
            case 0x09: return "Tab";
            case 0x1B: return "Escape";
        }
        return "vk " + v.ToString(CultureInfo.InvariantCulture);
    }

    static string MessageName(uint m)
    {
        switch (m)
        {
            case WM_KEYDOWN: return "WM_KEYDOWN";
            case WM_KEYUP: return "WM_KEYUP";
            case WM_SYSKEYDOWN: return "WM_SYSKEYDOWN";
            case WM_SYSKEYUP: return "WM_SYSKEYUP";
            case WM_SETFOCUS: return "WM_SETFOCUS";
            case WM_KILLFOCUS: return "WM_KILLFOCUS";
            case WM_ACTIVATE: return "WM_ACTIVATE";
            case WM_ACTIVATEAPP: return "WM_ACTIVATEAPP";
            case WM_MOUSEACTIVATE: return "WM_MOUSEACTIVATE";
            case WM_LBUTTONDOWN: return "WM_LBUTTONDOWN";
            case WM_RBUTTONDOWN: return "WM_RBUTTONDOWN";
            case WM_MBUTTONDOWN: return "WM_MBUTTONDOWN";
        }
        return "0x" + m.ToString("X", CultureInfo.InvariantCulture);
    }

    static IntPtr OnGetMessage(int code, IntPtr wParam, IntPtr lParam)
    {
        var self = instance;
        if (code >= 0 && wParam.ToInt64() == 1 && self != null)   // PM_REMOVE: the message is being taken
        {
            var m = (MSG)Marshal.PtrToStructure(lParam, typeof(MSG));
            switch (m.message)
            {
                case WM_KEYDOWN:
                case WM_KEYUP:
                case WM_SYSKEYDOWN:
                case WM_SYSKEYUP:
                    long lp = m.lParam.ToInt64();
                    Queue(string.Format("taken {0} {1} (scan 0x{2:X2}{3}) for {4}", MessageName(m.message), KeyName(m.wParam), (lp >> 16) & 0xFF,
                                        (lp & (1L << 30)) != 0 ? ", repeat" : "", self.WindowName(m.hwnd)));
                    break;
                case WM_LBUTTONDOWN:
                case WM_RBUTTONDOWN:
                case WM_MBUTTONDOWN:
                    Queue(string.Format("taken {0} for {1}", MessageName(m.message), self.WindowName(m.hwnd)));
                    break;
                case WM_INPUT:
                    RAWINPUTHEADER h;
                    uint size = (uint)Marshal.SizeOf(typeof(RAWINPUTHEADER));
                    if (GetRawInputData(m.lParam, RID_HEADER, out h, ref size, (uint)Marshal.SizeOf(typeof(RAWINPUTHEADER))) != unchecked((uint)-1))
                    {
                        if (h.dwType == 1)
                        {
                            System.Threading.Interlocked.Increment(ref rawKeyboard);
                            Queue("taken WM_INPUT (raw keyboard) for " + self.WindowName(m.hwnd));
                        }
                        else if (h.dwType == 0) System.Threading.Interlocked.Increment(ref rawMouse);
                    }
                    break;
            }
        }
        return CallNextHookEx(IntPtr.Zero, code, wParam, lParam);
    }

    static IntPtr OnCallWndProc(int code, IntPtr wParam, IntPtr lParam)
    {
        var self = instance;
        if (code >= 0 && self != null)
        {
            var c = (CWPSTRUCT)Marshal.PtrToStructure(lParam, typeof(CWPSTRUCT));
            switch (c.message)
            {
                case WM_SETFOCUS:
                case WM_KILLFOCUS:
                    Queue(string.Format("sent {0} to {1} ({2} {3})", MessageName(c.message), self.WindowName(c.hwnd),
                                        c.message == WM_SETFOCUS ? "from" : "to", self.WindowName(c.wParam)));
                    break;
                case WM_ACTIVATE:
                case WM_ACTIVATEAPP:
                case WM_MOUSEACTIVATE:
                    Queue(string.Format("sent {0} to {1} (wParam 0x{2:X})", MessageName(c.message), self.WindowName(c.hwnd), c.wParam.ToInt64()));
                    break;
            }
        }
        return CallNextHookEx(IntPtr.Zero, code, wParam, lParam);
    }

    /// <summary>"player", "player child", "host <class>" or "none", with the handle.</summary>
    string WindowName(IntPtr h)
    {
        if (h == IntPtr.Zero) return "none";
        if (h == playerWindow) return "player " + Hex(h);
        if (playerWindow != IntPtr.Zero && IsChild(playerWindow, h)) return "player child " + Hex(h);
        uint pid;
        GetWindowThreadProcessId(h, out pid);
        return (pid == processId ? "player-process " : "host ") + ClassOf(h) + " " + Hex(h);
    }

    void Update()
    {
        List<string> lines = null;
        lock (pendingLock)
        {
            if (pending.Count > 0)
            {
                lines = new List<string>(pending);
                pending.Clear();
            }
        }
        if (lines != null)
            foreach (string l in lines)
                Debug.Log("WMV: [input] " + l);

        // Which devices this process takes raw input from, and where Windows delivers it: with no target window the input
        // follows the keyboard focus; with one, it goes to that window while its input queue is the foreground one, or
        // always (INPUTSINK). Logged when it changes (the input backends register when they start).
        if (Time.unscaledTime >= nextRawCheck)
        {
            nextRawCheck = Time.unscaledTime + 1f;
            string raw = RegisteredRawInput();
            if (raw != lastRaw)
            {
                Log("raw input registered by this process: " + raw);
                lastRaw = raw;
            }
        }

        string focus = string.Format("focus {0}; active {1}; foreground {2}; Application.isFocused {3}", WindowName(GetFocus()),
                                     WindowName(GetActiveWindow()), WindowName(GetForegroundWindow()), Application.isFocused);
        if (focus != lastFocus)
        {
            Log(focus);
            lastFocus = focus;
        }

        string keys = "legacy " + LegacyKeys() + "; Input System " + InputSystemKeys();
        int rk = rawKeyboard, rm = rawMouse;
        if (keys != lastKeys)
        {
            Log(keys + string.Format("; WM_INPUT taken so far: keyboard {0}, mouse {1}", rk, rm));
            lastKeys = keys;
        }
        if (rk > 0 && lastRawKeyboard == 0)
            Log(string.Format("raw keyboard input (WM_INPUT) reaches the player's window thread ({0} so far)", rk));
        lastRawKeyboard = rk;

        // The camera: a line when its state (on, keys, input) changes; while only its position changes, at most ten a
        // second, and its position once more when it has stood still for a quarter second.
        string cam = CameraLine != null ? CameraLine() : "";
        float now = Time.unscaledTime;
        if (cam != prevCamera)
        {
            prevCamera = cam;
            lastCameraChange = now;
        }
        string state = cam.Split('|')[0];
        if (state != loggedState || (cam != loggedCamera && now - lastCameraLogTime >= 0.1f))
        {
            Log("camera " + cam.Replace("|", ";"));
            loggedState = state;
            loggedCamera = cam;
            lastCameraLogTime = now;
            cameraMoving = true;
        }
        else if (cameraMoving && now - lastCameraChange >= 0.25f)
        {
            if (cam != loggedCamera)
                Log("camera " + cam.Replace("|", ";"));
            Log("camera still");
            loggedCamera = cam;
            cameraMoving = false;
        }
    }

    float nextRawCheck;
    string lastRaw = "";

    /// <summary>GetRegisteredRawInputDevices in words: "keyboard -> follows the keyboard focus, mouse -> player 0x.. (INPUTSINK)".</summary>
    string RegisteredRawInput()
    {
        uint count = 0, size = (uint)Marshal.SizeOf(typeof(RAWINPUTDEVICE));
        GetRegisteredRawInputDevices(null, ref count, size);
        if (count == 0)
            return "none";
        var devices = new RAWINPUTDEVICE[count];
        uint got = GetRegisteredRawInputDevices(devices, ref count, size);
        if (got == unchecked((uint)-1))
            return "(could not be read)";
        var parts = new List<string>();
        for (int n = 0; n < (int)got; n++)
        {
            RAWINPUTDEVICE d = devices[n];
            string what = d.usUsagePage == 1 && d.usUsage == 6 ? "keyboard" : d.usUsagePage == 1 && d.usUsage == 2 ? "mouse"
                        : string.Format(CultureInfo.InvariantCulture, "usage {0}/{1}", d.usUsagePage, d.usUsage);
            var flags = new List<string>();
            if ((d.dwFlags & 0x100) != 0) flags.Add("INPUTSINK");
            if ((d.dwFlags & 0x30) == 0x30) flags.Add("NOLEGACY");
            if ((d.dwFlags & 0x2000) != 0) flags.Add("DEVNOTIFY");
            if ((d.dwFlags & 0x200) != 0) flags.Add("NOHOTKEYS");
            if ((d.dwFlags & 0x2000000) != 0) flags.Add("EXINPUTSINK");
            parts.Add(what + " -> " + (d.hwndTarget == IntPtr.Zero ? "follows the keyboard focus" : WindowName(d.hwndTarget)) +
                      (flags.Count > 0 ? " (" + string.Join(", ", flags.ToArray()) + ")" : ""));
        }
        return string.Join("; ", parts.ToArray());
    }

    string prevCamera = "", loggedState = "", loggedCamera = "";
    float lastCameraChange;
    bool cameraMoving;
    static string LegacyKeys()
    {
#if ENABLE_LEGACY_INPUT_MANAGER
        return Flags(Input.GetKey(KeyCode.W), Input.GetKey(KeyCode.A), Input.GetKey(KeyCode.S), Input.GetKey(KeyCode.D), Input.GetKey(KeyCode.Q),
                     Input.GetKey(KeyCode.E), Input.GetKey(KeyCode.Space), Input.GetKey(KeyCode.LeftShift) || Input.GetKey(KeyCode.RightShift));
#else
        return "(not built)";
#endif
    }

    static string InputSystemKeys()
    {
#if ENABLE_INPUT_SYSTEM
        var k = Keyboard.current;
        if (k == null) return "(no keyboard)";
        return (k.enabled ? "" : "(disabled) ") + Flags(k.wKey.isPressed, k.aKey.isPressed, k.sKey.isPressed, k.dKey.isPressed, k.qKey.isPressed,
                                                      k.eKey.isPressed, k.spaceKey.isPressed, k.shiftKey.isPressed);
#else
        return "(not built)";
#endif
    }

    static string Flags(bool w, bool a, bool s, bool d, bool q, bool e, bool space, bool shift)
    {
        var sb = new StringBuilder();
        if (w) sb.Append("W ");
        if (a) sb.Append("A ");
        if (s) sb.Append("S ");
        if (d) sb.Append("D ");
        if (q) sb.Append("Q ");
        if (e) sb.Append("E ");
        if (space) sb.Append("Space ");
        if (shift) sb.Append("Shift ");
        return sb.Length == 0 ? "none" : sb.ToString().TrimEnd();
    }
#else
    void Start()
    {
        Debug.Log("WMV: [input] the keyboard trace is for the Windows player only");
    }
#endif
}
