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
    private readonly DispatcherTimer _burstTimer;
    private DateTime _burstUntil = DateTime.MinValue;

    private IntPtr _hConsoleDisplay;
    private IntPtr _hMonitorPower;
    private IntPtr _hSessionDisplay;

    private WinEventDelegate? _foregroundDelegate;
    private IntPtr _foregroundHook;

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

        // Foreground-window hook — the critical anti-flash event
        _foregroundDelegate = OnForegroundChanged;
        _foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
            IntPtr.Zero, _foregroundDelegate, 0, 0, WINEVENT_OUTOFCONTEXT);

        // Defensive heartbeat: reapply every 15s no matter what
        _heartbeat = new DispatcherTimer { Interval = TimeSpan.FromSeconds(15) };
        _heartbeat.Tick += (_, _) => _gamma.Reapply();
        _heartbeat.Start();

        // Burst timer: while in the 2s window after a foreground change,
        // reapply every 50ms. Stops automatically when burst expires.
        _burstTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(50) };
        _burstTimer.Tick += (_, _) =>
        {
            if (DateTime.UtcNow >= _burstUntil) { _burstTimer.Stop(); return; }
            _gamma.Reapply();
        };
    }

    private void StartBurst()
    {
        _burstUntil = DateTime.UtcNow.AddSeconds(2);
        _gamma.Reapply();
        if (!_burstTimer.IsEnabled) _burstTimer.Start();
    }

    private void OnForegroundChanged(IntPtr hWinEventHook, uint eventType, IntPtr hwnd,
        int idObject, int idChild, uint thread, uint time)
        => StartBurst();

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        switch (msg)
        {
            case WM_DISPLAYCHANGE:
            case WM_DEVICECHANGE:
                _gamma.InvalidateAdapters();
                StartBurst();
                break;
            case WM_WTSSESSION_CHANGE:
                // 0x7 = WTS_SESSION_UNLOCK, 0x8 = WTS_CONSOLE_CONNECT
                StartBurst();
                break;
            case WM_POWERBROADCAST:
                // PBT_POWERSETTINGCHANGE = 0x8013
                if ((int)wParam == 0x8013) StartBurst();
                break;
            case WM_THEMECHANGED:
            case WM_DWMCOMPOSITIONCHANGED:
                StartBurst();
                break;
        }
        return IntPtr.Zero;
    }

    public void Dispose()
    {
        _heartbeat.Stop();
        _burstTimer.Stop();
        if (_foregroundHook != IntPtr.Zero) UnhookWinEvent(_foregroundHook);
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
    private const uint EVENT_SYSTEM_FOREGROUND = 0x0003;
    private const uint WINEVENT_OUTOFCONTEXT = 0x0000;

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
}
