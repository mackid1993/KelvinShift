#include "GammaWatchdog.h"
#include "GammaService.h"
#include <objbase.h>
#include <wtsapi32.h>

namespace {

const wchar_t* kClassName = L"KelvinShiftWatchdogWnd";

// There is exactly one watchdog for the process lifetime; the static hooks
// and timer callbacks route through this pointer.
GammaWatchdog* g_self = nullptr;
GammaService*  g_gamma = nullptr;

// Window messages (values fixed; defined locally to avoid header coupling).
const UINT kWmDisplayChange         = 0x007E;
const UINT kWmDeviceChange          = 0x0219;
const UINT kWmWtsSessionChange      = 0x02B1;
const UINT kWmPowerBroadcast        = 0x0218;
const UINT kWmThemeChanged          = 0x031A;
const UINT kWmDwmCompositionChanged = 0x031E;
const WPARAM kPbtPowerSettingChange = 0x8013;
const WPARAM kPbtApmResumeSuspend   = 0x0007;
const WPARAM kPbtApmResumeAutomatic = 0x0012;

// Power-setting GUIDs (inline so no extra SDK header/lib is needed).
const GUID kGuidConsoleDisplayState  =
    {0x6fe69556,0x704a,0x47a0,{0x8f,0x24,0xc2,0x8d,0x93,0x6f,0xda,0x47}};
const GUID kGuidMonitorPowerOn       =
    {0x02731015,0x4510,0x4526,{0x99,0xe6,0xe5,0xa1,0x7e,0xbd,0x1a,0xea}};
const GUID kGuidSessionDisplayStatus =
    {0x2b84c20e,0xad23,0x4ddf,{0x93,0xdb,0x05,0xff,0xbd,0x7e,0xfc,0xa5}};

} // namespace

GammaWatchdog::GammaWatchdog(GammaService& gamma) : gamma_(gamma)
{
    g_self = this;
    g_gamma = &gamma;

    HINSTANCE hInst = GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc = &GammaWatchdog::WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClassName;
    RegisterClassW(&wc);

    msgWindow_ = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, hInst, nullptr);

    // WTS session lock/unlock notifications.
    if (msgWindow_)
        WTSRegisterSessionNotification(msgWindow_, NOTIFY_FOR_THIS_SESSION);

    // Power-setting notifications (display state, monitor power, session
    // display) — their VidPnSourceIds can change across these events.
    GUID g1 = kGuidConsoleDisplayState, g2 = kGuidMonitorPowerOn, g3 = kGuidSessionDisplayStatus;
    hConsoleDisplay_ = RegisterPowerSettingNotification(msgWindow_, &g1, DEVICE_NOTIFY_WINDOW_HANDLE);
    hMonitorPower_   = RegisterPowerSettingNotification(msgWindow_, &g2, DEVICE_NOTIFY_WINDOW_HANDLE);
    hSessionDisplay_ = RegisterPowerSettingNotification(msgWindow_, &g3, DEVICE_NOTIFY_WINDOW_HANDLE);

    // Dedicated high-priority thread with a native pump installs the
    // SetWinEventHook from itself, so callbacks deliver here directly
    // (microsecond latency) instead of queued behind a dispatcher.
    hookThread_ = CreateThread(nullptr, 0, &GammaWatchdog::HookThreadMain, this, 0, nullptr);
    if (hookThread_)
        SetThreadPriority(hookThread_, THREAD_PRIORITY_HIGHEST);

    // Last-resort backstop at 30s. A 1-second heartbeat polled the GPU LUT
    // (GetDeviceGammaRamp, a ~4ms driver round-trip) every second — that was
    // ~0.4% idle CPU for nothing. Every real reset path (display, device,
    // session, power, resume) is handled by an event above; 30s is purely a
    // net for driver-level oddities and is effectively 0% idle CPU.
    heartbeat_.SetInterval(30000);
    heartbeat_.SetCallback([this] { gamma_.Reapply(); });
    heartbeat_.Start();
}

GammaWatchdog::~GammaWatchdog()
{
    heartbeat_.Stop();
    if (hookThreadId_)
        PostThreadMessageW(hookThreadId_, WM_QUIT, 0, 0);
    if (hookThread_)
    {
        WaitForSingleObject(hookThread_, 200);
        CloseHandle(hookThread_);
    }
    if (systemHook_) UnhookWinEvent(systemHook_);
    if (objectHook_) UnhookWinEvent(objectHook_);
    if (hConsoleDisplay_) UnregisterPowerSettingNotification(hConsoleDisplay_);
    if (hMonitorPower_)   UnregisterPowerSettingNotification(hMonitorPower_);
    if (hSessionDisplay_) UnregisterPowerSettingNotification(hSessionDisplay_);
    if (msgWindow_)
    {
        WTSUnRegisterSessionNotification(msgWindow_);
        DestroyWindow(msgWindow_);
    }
    g_self = nullptr;
    g_gamma = nullptr;
}

DWORD WINAPI GammaWatchdog::HookThreadMain(LPVOID param)
{
    auto* self = static_cast<GammaWatchdog*>(param);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Hook only the SYSTEM range — foreground / menu / dialog activations,
    // which is exactly when a shell flyout opens and resets gamma. The
    // OBJECT_CREATE..OBJECT_SHOW range was deliberately dropped: it fires
    // continuously system-wide (every window/object created anywhere) and
    // kept the process pinned at ~0.1% CPU at idle for no real benefit — the
    // 1-second heartbeat already covers anything the system hook misses.
    self->systemHook_ = SetWinEventHook(
        EVENT_SYSTEM_SOUND, EVENT_SYSTEM_MINIMIZEEND,
        nullptr, &GammaWatchdog::OnWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT);

    self->hookThreadId_ = GetCurrentThreadId();

    // Native pump — WinEvent callbacks dispatch inline here.
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}

void CALLBACK GammaWatchdog::OnWinEvent(HWINEVENTHOOK, DWORD eventType, HWND,
                                        LONG idObject, LONG, DWORD, DWORD)
{
    // For create/show, only react to the window itself (idObject 0), not its
    // child accessible objects — those fire constantly.
    if ((eventType == EVENT_OBJECT_CREATE || eventType == EVENT_OBJECT_SHOW)
        && idObject != 0)
        return;
    // Coalesce: the OBJECT_* range fires continuously system-wide, and a
    // read-back on every event is needless CPU at idle. One reapply per
    // frame is still comfortably inside the flash window.
    static ULONGLONG lastTick = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastTick < 16) return;
    lastTick = now;
    if (g_gamma) g_gamma->Reapply();
}

LRESULT CALLBACK GammaWatchdog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_gamma)
    {
        switch (msg)
        {
            case kWmDisplayChange:
            case kWmDeviceChange:
            case kWmWtsSessionChange:
            case kWmThemeChanged:
            case kWmDwmCompositionChanged:
                g_gamma->Reapply();
                break;
            case kWmPowerBroadcast:
                // Display power-state change, or wake from sleep / suspend.
                if (wParam == kPbtPowerSettingChange
                    || wParam == kPbtApmResumeSuspend
                    || wParam == kPbtApmResumeAutomatic)
                    g_gamma->Reapply();
                break;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
