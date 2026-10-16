// WmvKeyboardFocus.cs
//
// THE KEYBOARD FOR THE FREE CAMERA (docs/world-viewer/05-maps-viewer-ui.md section 22). What was measured with the
// keyboard trace (WmvInputTrace, -wmvInputTrace):
//   - both input backends of this player read the keyboard from Raw Input that the player registers for its own window,
//     without INPUTSINK (GetRegisteredRawInputDevices: "keyboard -> player, mouse -> player"); a WM_KEYDOWN reaching the
//     player's window changes neither Input.GetKey nor Keyboard.current;
//   - Windows gives raw keyboard input to the thread that has the keyboard focus. Embedded, the player's window shares
//     one input queue with the host's windows, and the keyboard stays where the host put it: in the host's viewport
//     panel after Enter world. A press in the player's window does not move it (its window procedure does not take the
//     focus on a press, and Windows moves it only on activation);
//   - raw mouse input does not need the focus, which is why the wheel always worked over the viewport.
// So the free camera takes the keyboard itself when a mouse button is pressed IN the player's window -- a press message
// taken from the queue for that window (WatchPresses: a thread hook on the window's thread that only notes it), not a
// press the input backends report while the pointer is merely over the viewport's rectangle, where a host dialog or menu
// may lie -- and reads keys only while the player's window has the keyboard (HasKeyboard). The host takes it back when the
// world view is left for another viewer; a click in any of the host's windows takes it, as any click there does.
//
// Win32 only. Elsewhere (the editor) the keyboard is the application's, and the calls say so.

using System;
using System.Globalization;
using System.Runtime.InteropServices;
using UnityEngine;

public static class WmvKeyboardFocus
{
#if UNITY_STANDALONE_WIN && !UNITY_EDITOR
    delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);
    delegate IntPtr HookProc(int code, IntPtr wParam, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)]
    struct MSG { public IntPtr hwnd; public uint message; public IntPtr wParam; public IntPtr lParam; public uint time; public int ptX, ptY; }

    [DllImport("user32.dll")] static extern IntPtr GetFocus();
    [DllImport("user32.dll")] static extern IntPtr SetFocus(IntPtr hwnd);
    [DllImport("user32.dll")] static extern bool IsChild(IntPtr parent, IntPtr child);
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] static extern IntPtr GetActiveWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumWindowsProc proc, IntPtr lParam);
    [DllImport("user32.dll")] static extern IntPtr SetWindowsHookEx(int idHook, HookProc lpfn, IntPtr hMod, uint dwThreadId);
    [DllImport("user32.dll")] static extern bool UnhookWindowsHookEx(IntPtr hhk);
    [DllImport("user32.dll")] static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("kernel32.dll")] static extern uint GetCurrentProcessId();

    const int WH_GETMESSAGE = 3;
    const uint WM_ACTIVATE = 0x0006, WA_ACTIVE = 1;
    const uint WM_LBUTTONDOWN = 0x0201, WM_RBUTTONDOWN = 0x0204, WM_MBUTTONDOWN = 0x0207;
    static bool quitHooked;

    static IntPtr playerWindow, pressHook;
    static HookProc pressProc;          // kept alive: the hook calls it
    static volatile bool pressSeen;

    /// <summary>The player's own window: the child of the -parentHWND window that belongs to this process (found once,
    /// again if it is gone). Without -parentHWND (a player on its own), its active window.</summary>
    public static IntPtr PlayerWindow
    {
        get
        {
            if (playerWindow != IntPtr.Zero && IsWindow(playerWindow))
                return playerWindow;
            playerWindow = Find();
            return playerWindow;
        }
    }

    static IntPtr Find()
    {
        string[] args = Environment.GetCommandLineArgs();
        IntPtr parent = IntPtr.Zero;
        for (int i = 0; i + 1 < args.Length; i++)
        {
            ulong v;
            if (args[i] == "-parentHWND" && ulong.TryParse(args[i + 1], NumberStyles.Integer, CultureInfo.InvariantCulture, out v))
                parent = new IntPtr((long)v);
        }
        if (parent == IntPtr.Zero)
            return GetActiveWindow();
        uint self = GetCurrentProcessId();
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (h, l) =>
        {
            uint pid;
            GetWindowThreadProcessId(h, out pid);
            if (pid != self)
                return true;
            found = h;
            return false;
        }, IntPtr.Zero);
        return found;
    }

    /// <summary>From now on, note every mouse press taken from the queue for the player's window (the hook only notes, the
    /// message goes on unchanged), until StopWatching -- which the free camera calls when it stops, and the player calls
    /// as it quits: a hook left in place made every exit hang until the host terminated the player (3 s).</summary>
    public static void WatchPresses()
    {
        if (!quitHooked)
        {
            quitHooked = true;
            Application.quitting += StopWatching;
        }
        if (pressHook != IntPtr.Zero)
            return;
        IntPtr w = PlayerWindow;
        if (w == IntPtr.Zero)
            return;
        uint pid;
        uint thread = GetWindowThreadProcessId(w, out pid);
        pressProc = OnGetMessage;
        pressHook = SetWindowsHookEx(WH_GETMESSAGE, pressProc, IntPtr.Zero, thread);
        if (pressHook == IntPtr.Zero)
            Debug.LogWarning("WMV: [input] the player could not watch its window's mouse presses: a click in the 3D view will not give it the keyboard");
    }

    static IntPtr OnGetMessage(int code, IntPtr wParam, IntPtr lParam)
    {
        if (code >= 0 && wParam.ToInt64() == 1)   // PM_REMOVE: the message is being taken
        {
            var m = (MSG)Marshal.PtrToStructure(lParam, typeof(MSG));
            if ((m.message == WM_LBUTTONDOWN || m.message == WM_RBUTTONDOWN || m.message == WM_MBUTTONDOWN) && playerWindow != IntPtr.Zero &&
                (m.hwnd == playerWindow || IsChild(playerWindow, m.hwnd)))
                pressSeen = true;
        }
        return CallNextHookEx(IntPtr.Zero, code, wParam, lParam);
    }

    /// <summary>No more noting of presses (the hook removed).</summary>
    public static void StopWatching()
    {
        if (pressHook != IntPtr.Zero)
            UnhookWindowsHookEx(pressHook);
        pressHook = IntPtr.Zero;
        pressSeen = false;
    }

    /// <summary>Was a mouse button pressed in the player's window since the last call?</summary>
    public static bool TakePressSeen()
    {
        bool seen = pressSeen;
        pressSeen = false;
        return seen;
    }

    /// <summary>Does the player's window (or a child of it) have the keyboard?</summary>
    public static bool HasKeyboard()
    {
        IntPtr w = PlayerWindow, f = GetFocus();
        return w != IntPtr.Zero && f != IntPtr.Zero && (f == w || IsChild(w, f));
    }

    /// <summary>The keyboard to the player's window (from this thread, which owns it; the input queue it shares with the
    /// host's windows makes that possible). True when it has it afterwards.</summary>
    public static bool TakeKeyboard(string why)
    {
        IntPtr w = PlayerWindow;
        if (w == IntPtr.Zero)
            return false;
        bool had = HasKeyboard();
        if (!had)
            SetFocus(w);
        bool has = HasKeyboard();
        // The player's window activated, as the documented embedding (-parentHWND) asks of the host: a child window is never
        // sent WM_ACTIVATE by Windows, and the player keeps its own idea of being the active window. Sent on every press
        // that gives it the keyboard, never WA_INACTIVE (the mouse must keep working in the other viewers).
        if (has)
            SendMessage(w, WM_ACTIVATE, new IntPtr(WA_ACTIVE), IntPtr.Zero);
        if (!had)
            Debug.Log("WMV: [input] the player's window " + (has ? "took the keyboard and was activated" : "could not take the keyboard") +
                      " (" + why + ")");
        return has;
    }
#else
    public static IntPtr PlayerWindow { get { return IntPtr.Zero; } }
    public static void WatchPresses() { }
    public static void StopWatching() { }
    public static bool TakePressSeen() { return false; }
    public static bool HasKeyboard() { return Application.isFocused; }
    public static bool TakeKeyboard(string why) { return Application.isFocused; }
#endif
}
