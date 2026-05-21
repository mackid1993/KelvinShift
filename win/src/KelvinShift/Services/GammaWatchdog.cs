using System;
using System.Runtime.InteropServices;
using System.Windows.Interop;
using System.Windows.Threading;

namespace KelvinShift.Services;

// Watchdog that re-applies gamma when external events nudge it.
//
// The shell-UI flash that Light Bulb and Iris exhibit happens because:
//   1. Action Center / Settings open → DWM briefly resets per-output gamma
//   2. The user sees the flash before any app reapplies
//
// Our defense: subscribe to every event that signals "something just touched
// gamma" and reapply within ~50ms. The most important hook is
// EVENT_SYSTEM_FOREGROUND — every time focus shifts to a flyout, we get a
// callback and reapply on a 50ms-for-2s tight burst. Lock/unlock, monitor
// hotplug, and display-config changes also invalidate the cached adapter
// handles since their VidPnSourceIds may change.
public sealed class GammaWatchdog : IDisposable
{
    private readonly GammaService _gamma;
    private readonly HwndSource _src;
    private readonly IntPtr _hwnd;
    private readonly DispatcherTimer _heartbeat;

    private System.Threading.Thread? _hookThread;
    private uint _hookThreadId;

    private IntPtr _hConsoleDisplay;
    private IntPtr _hMonitorPower;
    private IntPtr _hSessionDisplay;

    private WinEventDelegate? _foregroundDelegate;
    private IntPtr _foregroundHook;
    private IntPtr _objectShowHook;

    public GammaWatchdog(GammaService gamma)
    {
        _gamma = gamma;

        _src = new HwndSource(new HwndSourceParameters("KelvinShiftMsgWindow")
        {
            ParentWindow = HWND_MESSAGE,
            WindowStyle = 0,
        });
        _hwnd = _src.Handle;
        _src.AddHook(WndProc);

        // WTS session lock/unlock notifications
        WTSRegisterSessionNotification(_hwnd, NOTIFY_FOR_THIS_SESSION);

        // Power-setting notifications (display state, monitor power, session display)
        _hConsoleDisplay = RegisterPowerSettingNotification(_hwnd, ref GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);
        _hMonitorPower   = RegisterPowerSettingNotification(_hwnd, ref GUID_MONITOR_POWER_ON,    DEVICE_NOTIFY_WINDOW_HANDLE);
        _hSessionDisplay = RegisterPowerSettingNotification(_hwnd, ref GUID_SESSION_DISPLAY_STATUS, DEVICE_NOTIFY_WINDOW_HANDLE);

        // The SetWinEventHook callback must fire with sub-frame latency or
        // we miss the gamma reset window. Registering the hook from the WPF
        // UI thread queues WINEVENT_OUTOFCONTEXT events behind every other
        // dispatcher message — by the time our callback runs, the bright
        // frame has already been displayed.
        //
        // Dedicated TIME_CRITICAL thread with a native GetMessage pump
        // installs the hook from itself, so events deliver here directly.
        // The callback runs inside the GetMessage dispatch (microseconds),
        // and the gamma reapply lands before the next display refresh.
        _hookThread = new System.Threading.Thread(HookThreadMain)
        {
            Name = "KelvinShift-Hook",
            IsBackground = true,
            Priority = System.Threading.ThreadPriority.Highest,
        };
        _hookThread.SetApartmentState(System.Threading.ApartmentState.STA);
        _hookThread.Start();

        // Defensive heartbeat — covers events the WinEvent hooks can miss
        // (HDR toggle, sleep/wake without a window event).
        _heartbeat = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _heartbeat.Tick += (_, _) => _gamma.Reapply();
        _heartbeat.Start();
    }

    private void HookThreadMain()
    {
        // Install the WinEvent hooks from THIS thread so the callbacks
        // deliver here (not to the WPF dispatcher). OBJECT_CREATE fires
        // earlier in the window lifecycle than OBJECT_SHOW — before the
        // slide-in animation that resets gamma. SYSTEM range catches
        // foreground/menu/dialog activations.
        _foregroundDelegate = OnForegroundChanged;
        _foregroundHook = SetWinEventHook(
            EVENT_SYSTEM_SOUND, EVENT_SYSTEM_MINIMIZEEND,
            IntPtr.Zero, _foregroundDelegate, 0, 0, WINEVENT_OUTOFCONTEXT);
        _objectShowHook = SetWinEventHook(
            EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW,
            IntPtr.Zero, _foregroundDelegate, 0, 0, WINEVENT_OUTOFCONTEXT);

        _hookThreadId = GetCurrentThreadId();

        // Native GetMessage pump. Events arrive via PostThreadMessage when
        // the WinEvent hook fires; GetMessage drains them and invokes the
        // callback inline with microsecond latency.
        while (GetMessage(out var msg, IntPtr.Zero, 0, 0) > 0)
        {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(ref msg);
            DispatchMessage(ref msg);
        }
    }

    private void OnForegroundChanged(IntPtr hWinEventHook, uint eventType, IntPtr hwnd,
        int idObject, int idChild, uint thread, uint time)
    {
        if ((eventType == EVENT_OBJECT_CREATE || eventType == EVENT_OBJECT_SHOW)
            && idObject != 0) return;
        _gamma.Reapply();
    }

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        switch (msg)
        {
            case WM_DISPLAYCHANGE:
            case WM_DEVICECHANGE:
                _gamma.InvalidateAdapters();
                _gamma.Reapply();
                break;
            case WM_WTSSESSION_CHANGE:
                _gamma.Reapply();
                break;
            case WM_POWERBROADCAST:
                if ((int)wParam == 0x8013) _gamma.Reapply();  // PBT_POWERSETTINGCHANGE
                break;
            case WM_THEMECHANGED:
            case WM_DWMCOMPOSITIONCHANGED:
                _gamma.Reapply();
                break;
        }
        return IntPtr.Zero;
    }

    public void Dispose()
    {
        _heartbeat.Stop();
        if (_hookThreadId != 0)
            PostThreadMessage(_hookThreadId, WM_QUIT, IntPtr.Zero, IntPtr.Zero);
        _hookThread?.Join(200);
        if (_foregroundHook != IntPtr.Zero) UnhookWinEvent(_foregroundHook);
        if (_objectShowHook != IntPtr.Zero) UnhookWinEvent(_objectShowHook);
        if (_hConsoleDisplay != IntPtr.Zero) UnregisterPowerSettingNotification(_hConsoleDisplay);
        if (_hMonitorPower   != IntPtr.Zero) UnregisterPowerSettingNotification(_hMonitorPower);
        if (_hSessionDisplay != IntPtr.Zero) UnregisterPowerSettingNotification(_hSessionDisplay);
        WTSUnRegisterSessionNotification(_hwnd);
        _src.RemoveHook(WndProc);
        _src.Dispose();
    }

    // ── Native ────────────────────────────────────────────

    private static readonly IntPtr HWND_MESSAGE = new(-3);

    private const int WM_DISPLAYCHANGE        = 0x007E;
    private const int WM_DEVICECHANGE         = 0x0219;
    private const int WM_WTSSESSION_CHANGE    = 0x02B1;
    private const int WM_POWERBROADCAST       = 0x0218;
    private const int WM_THEMECHANGED         = 0x031A;
    private const int WM_DWMCOMPOSITIONCHANGED = 0x031E;

    private const int NOTIFY_FOR_THIS_SESSION = 0;
    private const int DEVICE_NOTIFY_WINDOW_HANDLE = 0;
    private const uint EVENT_SYSTEM_SOUND       = 0x0001;
    private const uint EVENT_SYSTEM_FOREGROUND  = 0x0003;
    private const uint EVENT_SYSTEM_MENUSTART   = 0x0004;
    private const uint EVENT_SYSTEM_MENUEND     = 0x0005;
    private const uint EVENT_SYSTEM_DIALOGSTART = 0x0010;
    private const uint EVENT_SYSTEM_MINIMIZEEND = 0x0017;
    private const uint EVENT_OBJECT_CREATE      = 0x8001;
    private const uint EVENT_OBJECT_SHOW        = 0x8002;
    private const uint WINEVENT_OUTOFCONTEXT    = 0x0000;

    private static Guid GUID_CONSOLE_DISPLAY_STATE  = new("6fe69556-704a-47a0-8f24-c28d936fda47");
    private static Guid GUID_MONITOR_POWER_ON       = new("02731015-4510-4526-99e6-e5a17ebd1aea");
    private static Guid GUID_SESSION_DISPLAY_STATUS = new("2b84c20e-ad23-4ddf-93db-05ffbd7efca5");

    private delegate void WinEventDelegate(IntPtr hWinEventHook, uint eventType, IntPtr hwnd,
        int idObject, int idChild, uint dwEventThread, uint dwmsEventTime);

    [DllImport("wtsapi32.dll")] private static extern bool WTSRegisterSessionNotification(IntPtr hwnd, int dwFlags);
    [DllImport("wtsapi32.dll")] private static extern bool WTSUnRegisterSessionNotification(IntPtr hwnd);

    [DllImport("user32.dll")] private static extern IntPtr RegisterPowerSettingNotification(IntPtr hRecipient, ref Guid PowerSettingGuid, int Flags);
    [DllImport("user32.dll")] private static extern bool UnregisterPowerSettingNotification(IntPtr Handle);

    [DllImport("user32.dll")] private static extern IntPtr SetWinEventHook(uint eventMin, uint eventMax, IntPtr hmodWinEventProc,
        WinEventDelegate lpfnWinEventProc, uint idProcess, uint idThread, uint dwFlags);
    [DllImport("user32.dll")] private static extern bool UnhookWinEvent(IntPtr hWinEventHook);

    // ── Native message pump (dedicated hook thread) ───────────
    private const uint WM_QUIT = 0x0012;

    [StructLayout(LayoutKind.Sequential)]
    private struct MSG
    {
        public IntPtr hwnd;
        public uint message;
        public IntPtr wParam;
        public IntPtr lParam;
        public uint time;
        public int pt_x;
        public int pt_y;
    }

    [DllImport("user32.dll")] private static extern int GetMessage(out MSG lpMsg, IntPtr hWnd, uint wMsgFilterMin, uint wMsgFilterMax);
    [DllImport("user32.dll")] private static extern bool TranslateMessage(ref MSG lpMsg);
    [DllImport("user32.dll")] private static extern IntPtr DispatchMessage(ref MSG lpMsg);
    [DllImport("user32.dll")] private static extern bool PostThreadMessage(uint idThread, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
}
